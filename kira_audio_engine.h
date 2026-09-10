#pragma once

#include <Arduino.h>

// ============================================================
// KIRA NEXT - AUDIO ENGINE / FRAME FRONTEND
// ============================================================
// Central 16 kHz mono input contract. The proven ESP-SR bridge remains the
// physical I2S owner; KIRA observes the same samples without a second read.
// ============================================================

constexpr uint32_t KIRA_AUDIO_INPUT_RATE = 16000;
constexpr size_t KIRA_AUDIO_FRAME_SAMPLES = 320;
constexpr uint32_t KIRA_AUDIO_FRAME_MS = 20;

bool kiraAudioEngineBegin();

void kiraAudioEngineObserveInput(
  const int16_t* samples,
  size_t count
);

void kiraAudioEngineStartUtterance();
void kiraAudioEngineCancelUtterance();

bool kiraAudioEngineSpeechSeen();
bool kiraAudioEngineSpeechEnded(uint32_t silenceMs);

uint32_t kiraAudioEngineInputFrames();
