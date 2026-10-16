/*
 * ============================================================================
 *  EggCandlingSystem.ino  --  Standalone Egg Age Predictor
 * ----------------------------------------------------------------------------
 *  Fully standalone firmware: reads the TCS34725 color sensor and HX711
 *  load cell, waits for a STABLE + high-confidence reading, runs an
 *  on-device Random Forest classifier, and shows the predicted age bucket
 *  on a local 16x2 I2C LCD. No WiFi, no cloud upload, no remote commands --
 *  nothing leaves the device. See docs/USAGE_GUIDE.md for the operator
 *  manual and docs/RANDOM_FOREST_MODEL.md for how the model was trained.
 *
 *  MAIN LOOP DESIGN:
 *  A single-threaded, non-blocking cooperative scheduler -- every task
 *  below is gated by its own millis()-based interval check and returns
 *  immediately if it isn't due yet, so no task ever blocks another
 *  (critical for the encoder/LCD to stay responsive while sensors are
 *  being read).
 * ============================================================================
 */

#include <Arduino.h>
#include "Config.h"
#include "ColorSensorManager.h"
#include "LoadCellManager.h"
#include "StabilityDetector.h"
#include "EncoderManager.h"
#include "DisplayManager.h"
#include "EggAgePredictor.h"

// ============================================================================
// GLOBAL SUBSYSTEM INSTANCES
// ============================================================================
ColorSensorManager  colorSensor;
LoadCellManager      loadCell;
StabilityDetector    stabilityDetector;
EncoderManager        encoderManager;
DisplayManager        displayManager;

// ============================================================================
// LATEST FILTERED READINGS
// ============================================================================
ColorReading      latestColor;
float              latestWeight = 0.0f;
StabilityResult    latestStability;

// ============================================================================
// EGG AGE PREDICTION STATE
// ----------------------------------------------------------------------------
// A prediction is only (re)computed when the reading transitions into the
// "trusted" zone (STABLE + confidence >= AGE_PREDICTION_CONFIDENCE_THRESHOLD)
// -- not on every single loop cycle -- both to avoid redundant classifier
// runs and so the LCD result doesn't flicker between equally-plausible
// buckets on borderline confidence noise. It's cleared the moment the
// reading drops back out of that trusted zone (e.g. egg removed/repositioned),
// so a stale prediction is never shown for a reading that's no longer valid.
// ============================================================================
bool          predictionReady = false;
EggAgeBucket  predictedBucket = EggAgeBucket::UNKNOWN;

// ============================================================================
// LCD SCROLL STATE
// ============================================================================
int currentScreenIndex = 0; // wraps within [0, (int)ScrollScreen::SCREEN_COUNT)

// ============================================================================
// SCHEDULER STATE
// ============================================================================
uint32_t lastSensorReadMs  = 0;
uint32_t lastStatusBlinkMs = 0;
bool statusLedState         = false;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void taskReadSensors();
void taskUpdateStatusLed();
void taskUpdateEncoderAndDisplay();
void printBootBanner();

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(200); // let the serial monitor catch up on boards that need it

    printBootBanner();

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    // ---- 1. Color sensor (TCS34725) ----------------------------------------
    Serial.println(F("[INIT] Initializing color sensor..."));
    if (!colorSensor.begin()) {
        Serial.println(F("[ERROR] TCS34725 not detected! Check wiring (SDA=21, SCL=22)."));
    } else {
        Serial.println(F("[OK] Color sensor ready."));
    }

    // ---- 2. Load cell (HX711) ----------------------------------------------
    Serial.println(F("[INIT] Initializing load cell..."));
    if (!loadCell.begin()) {
        Serial.println(F("[ERROR] HX711 not detected! Check wiring (DOUT=19, SCK=18)."));
    } else {
        Serial.println(F("[OK] Load cell ready and tared."));
    }

    // ---- 3. Rotary encoder (KY-040) ----------------------------------------
    Serial.println(F("[INIT] Initializing rotary encoder..."));
    encoderManager.begin();
    Serial.println(F("[OK] Encoder ready."));

    // ---- 4. LCD (I2C, shares the bus already brought up by the color sensor) -
    Serial.println(F("[INIT] Initializing LCD..."));
    displayManager.begin();
    Serial.println(F("[OK] LCD ready."));

    Serial.println(F("[INFO] Rotate encoder to scroll data; long-press to tare."));
    Serial.println(F("[READY] Egg Age Predictor operational.\n"));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    taskReadSensors();             // gated by SENSOR_READ_INTERVAL_MS
    taskUpdateStatusLed();         // gated by STATUS_LED_BLINK_MS
    taskUpdateEncoderAndDisplay(); // encoder scroll/tare + LCD refresh, gated internally
}

// ============================================================================
// TASK: Read + filter sensors, update stability, run the age predictor.
// ----------------------------------------------------------------------------
// Gated to SENSOR_READ_INTERVAL_MS. Runs the color sensor's batch read
// (trimmed-mean filtered internally) and, once the HX711 reports fresh
// data available, the load cell's median+moving-average filter chain.
// Feeds both into StabilityDetector, then decides whether to (re)run the
// age classifier based on the trusted-zone gate described above.
// ============================================================================
void taskReadSensors() {
    uint32_t now = millis();
    if (now - lastSensorReadMs < SENSOR_READ_INTERVAL_MS) return;
    lastSensorReadMs = now;

    latestColor = colorSensor.readFiltered();

    if (loadCell.isDataReady()) {
        latestWeight = loadCell.readFiltered();
    }

    latestStability = stabilityDetector.update(
        latestWeight,
        latestColor.red, latestColor.green, latestColor.blue, latestColor.clear
    );

    // ---- Egg age prediction gate --------------------------------------
    // Only trust a reading enough to classify it once it's STABLE AND at
    // or above AGE_PREDICTION_CONFIDENCE_THRESHOLD (see Config.h) -- a
    // single classifier run on a still-settling reading would be no more
    // meaningful than the noisy-input 14-day exact-day model was during
    // training (see docs/RANDOM_FOREST_MODEL.md).
    bool trustedReading = (latestStability.status == StabilityStatus::STABLE) &&
                           (latestStability.confidence >= AGE_PREDICTION_CONFIDENCE_THRESHOLD);

    if (trustedReading) {
        if (!predictionReady) {
            // Just crossed into the trusted zone this cycle -- classify once.
            EggFeatures features{};
            features.weight    = latestWeight;
            features.red       = latestColor.red;
            features.green     = latestColor.green;
            features.blue      = latestColor.blue;
            features.clear     = latestColor.clear;
            features.lux       = latestColor.lux;
            features.colorTemp = latestColor.colorTemperature;
            features.normR     = latestColor.normalizedR;
            features.normG     = latestColor.normalizedG;
            features.normB     = latestColor.normalizedB;

            // predict() internally checks the >14-day out-of-range floor
            // thresholds BEFORE running the forest -- see EggAgePredictor.h.
            predictedBucket = EggAgePredictor::predict(features);
            predictionReady = true;

            Serial.print(F("[PREDICT] Age bucket: "));
            Serial.println(EggAgePredictor::bucketLabelLong(predictedBucket));
        }
        // else: already classified this stable reading -- don't re-run the
        // forest every cycle for no benefit.
    } else {
        // Dropped out of the trusted zone (egg moved/removed, reading still
        // settling, etc.) -- clear so a stale result is never displayed
        // against a reading that's no longer valid.
        predictionReady = false;
        predictedBucket = EggAgeBucket::UNKNOWN;
    }
}

// ============================================================================
// TASK: Blink the onboard status LED as a simple "still running" heartbeat.
// ============================================================================
void taskUpdateStatusLed() {
    uint32_t now = millis();
    if (now - lastStatusBlinkMs < STATUS_LED_BLINK_MS) return;
    lastStatusBlinkMs = now;

    statusLedState = !statusLedState;
    digitalWrite(PIN_STATUS_LED, statusLedState ? HIGH : LOW);
}

// ============================================================================
// TASK: Poll the encoder (rotation + push switch) and refresh the LCD.
// ----------------------------------------------------------------------------
// Rotation is interrupt-driven (see EncoderManager) so scrolling never lags
// or skips a step regardless of what else loop() is doing this cycle; this
// task just drains the accumulated step count and applies it to the screen
// index. The switch's long-press event triggers a scale tare.
//
// The LCD write itself is internally gated to LCD_REFRESH_INTERVAL_MS and
// change-detected per row inside DisplayManager, so calling update() every
// loop() iteration here is both correct and cheap -- this is what keeps the
// display smooth with zero perceptible glitching.
// ============================================================================
void taskUpdateEncoderAndDisplay() {
    encoderManager.update();

    int rotation = encoderManager.consumeRotation();
    if (rotation != 0) {
        int screenCount = (int)ScrollScreen::SCREEN_COUNT;
        currentScreenIndex = (currentScreenIndex + rotation) % screenCount;
        if (currentScreenIndex < 0) currentScreenIndex += screenCount; // wrap negative
    }

    if (encoderManager.tareHoldEvent()) {
        Serial.println(F("[ENCODER] Long-press detected. Re-zeroing scale..."));
        loadCell.tare();
        Serial.println(F("[OK] Scale re-tared."));
    }
    // encoderManager.shortPressEvent() is reserved for future use --
    // intentionally unhandled for now.

    displayManager.update(
        static_cast<ScrollScreen>(currentScreenIndex),
        latestWeight,
        latestColor,
        latestStability,
        predictionReady,
        predictedBucket
    );
}

// ============================================================================
// UTILITY: Boot banner for the serial monitor
// ============================================================================
void printBootBanner() {
    Serial.println();
    Serial.println(F("================================================"));
    Serial.println(F("   Standalone Egg Age Predictor"));
    Serial.print(F("   Firmware v"));
    Serial.println(F(FIRMWARE_VERSION));
    Serial.print(F("   Device: "));
    Serial.println(F(DEVICE_ID));
    Serial.println(F("================================================"));
}
