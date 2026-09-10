#pragma once

#include <Arduino.h>

// ============================================================
// KIRA VOICE BRIDGE
// ============================================================
//
// WakeNet, MultiNet and automatic STT stay unchanged.
// Phase 5L.2 adds safe global-TTS coordination.
// ============================================================

bool kiraVoiceBegin();
bool kiraVoiceReady();

bool kiraVoiceTakeCommand(
  String& command
);

bool kiraVoicePauseForTts();

void kiraVoiceResumeAfterTts(
  bool pausedByTts
);

void kiraVoiceCommandFinished(
  bool speechPlayed
);
