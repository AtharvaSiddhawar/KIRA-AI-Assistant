#pragma once

#include <Arduino.h>

// ============================================================
// KIRA PHYSICAL DEVICE I/O
// ============================================================

void kiraDeviceIOBegin();

void kiraDeviceIOApply(
  bool mainLightOn,
  bool fanOn,
  bool chargerOn,
  bool secondLightOn
);