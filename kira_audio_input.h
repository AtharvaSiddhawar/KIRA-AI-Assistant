#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

// KIRA Next compatibility placeholder.
// Group 1 SAFE WAKE HOTFIX deliberately does NOT place a second reader
// between I2S0 and ESP-SR. Espressif's SR fill callback remains the
// real-time input owner because that is the proven WakeNet path.
//
// Keep this tab; do not delete it. A future input-owner implementation can
// replace this interface after we have a wake-compatible zero-copy design.

bool kiraAudioInputBegin(I2SClass& mic);
void kiraAudioInputEnd();
bool kiraAudioInputReady();

size_t kiraAudioInputReadForSr(
  void* out,
  size_t bytes,
  uint32_t timeoutMs
);

void kiraAudioInputStartUtterance();
bool kiraAudioInputPreserveUtterance();
void kiraAudioInputDiscardUtterance();

bool kiraAudioInputCapturedView(
  const int16_t*& pcm,
  size_t& samples
);

bool kiraAudioInputCaptureOverflowed();

uint32_t kiraAudioInputFrames();
uint32_t kiraAudioInputDroppedFrames();
