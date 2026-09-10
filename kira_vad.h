#pragma once

#include <Arduino.h>

// ============================================================
// KIRA NEXT - VAD V2
// ============================================================
// 16 kHz / 20 ms frame-oriented adaptive speech detector.
// It learns steady ambient/fan noise while KIRA is idle and requires
// multiple voice-like frames before declaring actual speech.
// ============================================================

void kiraVadBegin();
void kiraVadStartUtterance();
void kiraVadCancelUtterance();

void kiraVadProcessBlock(
  const int16_t* samples,
  size_t count
);

bool kiraVadSpeechSeen();
bool kiraVadSpeechActive();
bool kiraVadSpeechEnded(uint32_t silenceMs);

uint8_t kiraVadConfidence();
uint32_t kiraVadNoiseLevel();
uint32_t kiraVadLastLevel();
uint32_t kiraVadUtteranceDurationMs();
