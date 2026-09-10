#pragma once

#include <Arduino.h>

// ============================================================
// KIRA PHASE 5H - AUTOMATION SCENES
// ============================================================
//
// Scenes only change KIRA's logical device states.
// The existing kira_device_io.cpp remains the only layer that
// touches physical relay GPIO.
//
// This keeps Serial, voice and future touch on the same
// Universal Brain path.
// ============================================================

void kiraScenesBegin();

bool kiraSceneHandleCommand(
  String q,
  bool& mainLightOn,
  bool& fanOn,
  bool& chargerOn,
  bool& secondLightOn
);
