#include <Wire.h>
#include "Adafruit_TCS34725.h"

// ==========================================
// KONFIGURASI TCS34725
// ==========================================
#define INTEGRATION_TIME_MS 50.4
#define SENSOR_GAIN        4.0

// Parameter DN40
#define GLASS_ATTENUATION  1.0
#define DEVICE_FACTOR      310.0

#define R_COEF  0.136
#define G_COEF  1.000
#define B_COEF -0.444

Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_1X
);

// ==========================================
// FUNGSI PERHITUNGAN LUX DN40
// ==========================================
float calculateLuxDN40(
  uint16_t r,
  uint16_t g,
  uint16_t b,
  uint16_t c
) {

  // ----------------------------------------
  // 1. Kompensasi IR
  // ----------------------------------------
  float IR = 0;

  if ((uint32_t)r + g + b > c) {
    IR = ((float)r + g + b - c) / 2.0;
  }

  float rComp = r - IR;
  float gComp = g - IR;
  float bComp = b - IR;

  // ----------------------------------------
  // 2. Perhitungan G1
  // ----------------------------------------
  float G1 =
      (R_COEF * rComp) +
      (G_COEF * gComp) +
      (B_COEF * bComp);

  // ----------------------------------------
  // 3. Counts Per Lux (CPL)
  // ----------------------------------------
  float CPL =
      (INTEGRATION_TIME_MS * SENSOR_GAIN) /
      (GLASS_ATTENUATION * DEVICE_FACTOR);

  if (CPL <= 0) {
    return 0;
  }

  // ----------------------------------------
  // 4. Lux
  // ----------------------------------------
  float lux = G1 / CPL;

  // Lux tidak boleh negatif
  if (lux < 0) {
    lux = 0;
  }

  return lux;
}

// ==========================================
// CEK SATURASI
// ==========================================
bool checkSaturation(uint16_t c) {

  // ATIME untuk 50 ms ≈ 0xEB
  // 256 - 235 = 21
  uint16_t saturation = 1024 * 21;

  // Ripple saturation untuk integration
  // time < 150 ms
  saturation = saturation - saturation / 4;

  // Hasil ≈ 16128 counts
  if (c >= saturation) {
    return true;
  }

  return false;
}

// ==========================================
// SETUP
// ==========================================
void setup() {

  Serial.begin(9600);
  Wire.begin();

  Serial.println();
  Serial.println("=====================================");
  Serial.println(" TCS34725 - PERHITUNGAN LUX DN40");
  Serial.println("=====================================");
  Serial.println("Integration Time : 50 ms");
  Serial.println("Gain             : 4X");
  Serial.println();

  if (tcs.begin()) {
    Serial.println("Sensor TCS34725 berhasil terdeteksi.");
  } 
  else {
    Serial.println("Sensor TCS34725 tidak ditemukan!");
    
    while (1);
  }
}

// ==========================================
// LOOP
// ==========================================
void loop() {

  uint16_t r, g, b, c;

  // Membaca data mentah
  tcs.getRawData(&r, &g, &b, &c);

  Serial.println("-------------------------------------");

  Serial.print("Red         : ");
  Serial.println(r);

  Serial.print("Green       : ");
  Serial.println(g);

  Serial.print("Blue        : ");
  Serial.println(b);

  Serial.print("Clear       : ");
  Serial.println(c);

  // Cek saturasi
  if (checkSaturation(c)) {

    Serial.println("STATUS      : SATURASI!");
    Serial.println("Lux         : DATA TIDAK VALID");

  } 
  else {

    float lux = calculateLuxDN40(r, g, b, c);

    Serial.println("STATUS      : NORMAL");

    Serial.print("Lux DN40    : ");
    Serial.print(lux, 2);
    Serial.println(" lux");
  }

  delay(1000);
}