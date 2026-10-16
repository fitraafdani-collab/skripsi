#include "HX711.h"

HX711 scale;

#define DT 6
#define SCK 7

float calibration_factor = 1;
float beratReferensi = 417.0; 
bool sudahKalibrasi = false;

void setup() {
  Serial.begin(9600);
  Serial.println("=== KALIBRASI HX711 ===");
  Serial.println("1. Pastikan load cell kosong");
  
  scale.begin(DT, SCK);
  scale.set_scale();
  scale.tare(); // nolkan timbangan
  delay(2000);

  Serial.println("2. Letakkan beban referensi sekarang...");
  delay(5000); // waktu untuk menaruh beban
}

void loop() {
  if (!sudahKalibrasi) {
    long bacaanRaw = scale.read_average(10); // rata-rata 10 bacaan
    calibration_factor = (float)bacaanRaw / beratReferensi; // koreksi: raw dibagi berat referensi
    scale.set_scale(calibration_factor);
    sudahKalibrasi = true;
    Serial.print("Calibration Factor: ");
    Serial.println(calibration_factor, 5);
    Serial.println("Mulai pembacaan...");
  }

  float berat = scale.get_units(5); 
  if (berat < 0) berat = 0;
  Serial.print("Berat: ");
  Serial.print(berat, 2);
  Serial.println(" gram");
  delay(1000);
}