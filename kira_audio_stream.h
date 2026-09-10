#pragma once

#include <Arduino.h>

// ============================================================
// KIRA NEXT GROUP B - NON-BLOCKING MICROPHONE UPLINK
// ============================================================
// Producer: ESP-SR microphone tap -> bounded 20 ms PCM queue
// Consumer: Arduino main loop -> 60 ms Opus -> Gateway binary frame
// The producer never performs network I/O.
// ============================================================

bool kiraAudioStreamBegin();
void kiraAudioStreamService();

void kiraAudioStreamStartTurn();
void kiraAudioStreamCancelTurn();

void kiraAudioStreamObserveInput(
  const int16_t* samples,
  size_t count
);

bool kiraAudioStreamActive();
uint32_t kiraAudioStreamQueuedFrames();
uint32_t kiraAudioStreamDroppedFrames();
uint32_t kiraAudioStreamSentPackets();
uint32_t kiraAudioStreamSentBytes();
