#pragma once

#include <Arduino.h>
#include <SPI.h>

// Starts KIRA's two independent hardware SPI controllers:
//
//   TFT -> FSPI / SPI2
//   SD  -> HSPI / SPI3
//
// Safe to call multiple times.
bool kiraSpiBusesBegin();

SPIClass& kiraTftSPI();
SPIClass& kiraSdSPI();

bool kiraTftSPIReady();
bool kiraSdSPIReady();
