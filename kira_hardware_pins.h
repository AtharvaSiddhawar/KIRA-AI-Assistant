#pragma once

// ============================================================
// KIRA PERMANENT HARDWARE PIN MAP — SEPARATE BUS WIRING
// ESP32-S3-WROOM-1 N16R8
// ============================================================
//
// IMPORTANT:
// - No SPI signal is shared between TFT, Touch, and SD.
// - Each module may have its own physical GND wire, but all directly
//   connected GPIO modules still need the same electrical ground reference.
// - TFT LED/backlight is HARDWIRED TO 3.3V. It is NOT an ESP32 GPIO.
// ============================================================

namespace KiraPins {


// -------------------- TFT / DISPLAY --------------------
// Dedicated hardware SPI bus: FSPI / SPI2
constexpr int TFT_SCK  = 12;
constexpr int TFT_MOSI = 11;
constexpr int TFT_MISO = 13;
constexpr int TFT_CS   = 10;
constexpr int TFT_DC   = 9;
constexpr int TFT_RST  = 8;

// TFT LED / BL:
//   HARDWIRED -> 3.3V
//   No GPIO assignment.


// -------------------- TOUCH / XPT2046 --------------------
// Dedicated SOFTWARE SPI bus.
// This phase only reserves/fixes the permanent pins.
// Touch protocol driver comes next.
constexpr int TOUCH_CLK  = 41;
constexpr int TOUCH_DIN  = 42;
constexpr int TOUCH_DO   = 47;
constexpr int TOUCH_CS   = 48;
constexpr int TOUCH_IRQ  = 7;


// -------------------- SD CARD --------------------
// Dedicated hardware SPI bus: HSPI / SPI3
constexpr int SD_SCK  = 4;
constexpr int SD_MOSI = 5;
constexpr int SD_MISO = 6;
constexpr int SD_CS   = 21;


// -------------------- INMP441 MICROPHONE --------------------
constexpr int MIC_SCK  = 38;
constexpr int MIC_WS   = 39;
constexpr int MIC_DOUT = 40;


// -------------------- MAX98357A SPEAKER AMP --------------------
constexpr int AMP_BCLK = 15;
constexpr int AMP_LRC  = 16;
constexpr int AMP_DIN  = 17;
constexpr int AMP_SD   = 18;


// -------------------- RELAY --------------------
constexpr int RELAY_IN = 14;


} // namespace KiraPins
