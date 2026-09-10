#pragma once

#include <Arduino.h>

// ============================================================
// KIRA NEXT - AUDIO OUTPUT QUEUE V1
// ============================================================
//
// Dedicated PSRAM playback ring + FreeRTOS playback task.
//
// Stage V1 deliberately keeps the proven low-level speaker bridge in
// kira_voice.cpp so beeps/legacy audio remain compatible, but TTS no longer
// writes timing-sensitive PCM directly from the network/TTS task.
//
// Producer:
//   TTS -> stereo PCM -> PSRAM ring
//
// Consumer:
//   AudioOutput task -> proven I2S1 speaker bridge
//
// This removes HTTP/network timing from the I2S playback path.
// ============================================================

bool kiraAudioOutputBegin();

bool kiraAudioOutputStart(
  uint32_t sampleRate
);

size_t kiraAudioOutputWrite(
  const uint8_t* data,
  size_t bytes,
  uint32_t stallTimeoutMs = 5000
);

bool kiraAudioOutputFinish(
  uint32_t drainTimeoutMs = 30000
);

void kiraAudioOutputAbort();

uint32_t kiraAudioOutputBufferedBytes();
uint32_t kiraAudioOutputBufferedMs();
uint32_t kiraAudioOutputUnderruns();

bool kiraAudioOutputActive();
