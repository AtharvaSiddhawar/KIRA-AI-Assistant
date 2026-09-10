#include "kira_audio_input.h"

// SAFE WAKE HOTFIX:
// This module is intentionally passive. The proven ESP-SR fill callback
// reads I2S0 directly and mirrors those exact PCM blocks into KIRA Audio Core.

namespace {
bool readyFlag = false;
}

bool kiraAudioInputBegin(I2SClass& mic) {
  (void)mic;
  readyFlag = false;
  return true;
}

void kiraAudioInputEnd() {
  readyFlag = false;
}

bool kiraAudioInputReady() {
  return readyFlag;
}

size_t kiraAudioInputReadForSr(
  void* out,
  size_t bytes,
  uint32_t timeoutMs
) {
  (void)out;
  (void)bytes;
  (void)timeoutMs;
  return 0;
}

void kiraAudioInputStartUtterance() {}

bool kiraAudioInputPreserveUtterance() {
  return false;
}

void kiraAudioInputDiscardUtterance() {}

bool kiraAudioInputCapturedView(
  const int16_t*& pcm,
  size_t& samples
) {
  pcm = nullptr;
  samples = 0;
  return false;
}

bool kiraAudioInputCaptureOverflowed() {
  return false;
}

uint32_t kiraAudioInputFrames() {
  return 0;
}

uint32_t kiraAudioInputDroppedFrames() {
  return 0;
}
