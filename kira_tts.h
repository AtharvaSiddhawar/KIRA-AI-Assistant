#pragma once

#include <Arduino.h>

// ============================================================
// KIRA PHASE 5L.2 - GLOBAL ELLI TTS
// ============================================================

bool kiraTtsConfigured();
bool kiraTtsSpeak(const String& text);

void kiraTtsQueue(
  const String& text
);

bool kiraTtsFlushPending();

uint8_t kiraTtsPendingCount();
