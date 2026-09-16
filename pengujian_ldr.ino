#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Gunakan alamat 0x27 atau 0x3F (tergantung jenis modul)
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  lcd.init();         // Inisialisasi LCD
  lcd.backlight();    // Nyalakan lampu backlight

  lcd.setCursor(0, 0);
  lcd.print("2110443010");

  lcd.setCursor(0, 1);
  lcd.print("RIMA TSAMRATUL AINI");
}

void loop() {
  // Tidak ada yang perlu di loop
}