#pragma once

#include <Arduino.h>

// ============================================================
// KIRA NEXT GROUP B - AUDIO CODEC ABSTRACTION V2
// ============================================================

enum KiraAudioCodec : uint8_t {
  KIRA_AUDIO_CODEC_PCM16 = 0,
  KIRA_AUDIO_CODEC_OPUS
};

bool kiraAudioCodecBegin();

bool kiraAudioCodecAvailable(
  KiraAudioCodec codec
);

const char* kiraAudioCodecName(
  KiraAudioCodec codec
);

KiraAudioCodec kiraAudioCodecNetworkPreference();

// 16 kHz mono Opus. For Group B, the intended input frame is 960 samples
// (60 ms). Returns encoded byte count, or 0 when unavailable/failure.
size_t kiraAudioCodecEncodeOpus(
  const int16_t* pcm,
  size_t samples,
  uint8_t* out,
  size_t outCapacity
);

// Prepared for Group C downlink playback. Returns decoded PCM samples.
size_t kiraAudioCodecDecodeOpus(
  const uint8_t* packet,
  size_t packetBytes,
  int16_t* pcmOut,
  size_t pcmCapacitySamples
);

bool kiraAudioCodecOpusBackendPresent();
