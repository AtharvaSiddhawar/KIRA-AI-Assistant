#pragma once

#include <Arduino.h>
#include <ESP_SR.h>

// ============================================================
// KIRA NEXT GROUP 1 - WAKE SERVICE
// ============================================================
// Current model: stock WakeNet "Hi ESP".
// Future custom model can be swapped behind this interface without
// changing voice/router/runtime code.
// ============================================================

bool kiraWakeBegin();

bool kiraWakeAcceptDetection(
  sr_event_t event
);

void kiraWakeSetSpeakerActive(
  bool active
);

void kiraWakeArmIdle();

uint32_t kiraWakeAcceptedCount();
uint32_t kiraWakeRejectedCount();
