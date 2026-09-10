#pragma once

#include <Arduino.h>


#ifndef KIRA_ELLI_DISPLAY_ENABLED
#define KIRA_ELLI_DISPLAY_ENABLED 1
#endif


// ============================================================
// ELLI VISUAL STATES
// ============================================================

enum ElliVisualState {

  ELLI_VISUAL_IDLE,

  ELLI_VISUAL_LISTENING,

  ELLI_VISUAL_THINKING,

  ELLI_VISUAL_SPEAKING,

  ELLI_VISUAL_HAPPY,

  ELLI_VISUAL_EXCITED,

  ELLI_VISUAL_SAD,

  ELLI_VISUAL_CONFUSED,

  ELLI_VISUAL_SLEEPY,

  ELLI_VISUAL_ANGRY
};


// ============================================================
// DISPLAY ENGINE
// ============================================================

void elliVisualBegin();

void elliVisualUpdate();

bool elliVisualReady();


// ============================================================
// STATE CONTROL
// ============================================================

void elliVisualSetState(
  ElliVisualState state
);

ElliVisualState elliVisualGetState();


// ============================================================
// KIRA BRAIN HOOKS
// ============================================================

void elliVisualCommandStart();

void elliVisualCommandEnd();

void elliVisualSpeakText(
  const String& text
);

void elliVisualNotifyActivity();

// Phase 5G: after the current reply animation finishes,
// remain in the approved sleepy animation.
void elliVisualSleepAfterSpeech();

// ============================================================
// PHASE 5M - REAL AUDIO-SYNCED MOUTH
// ============================================================
// level: 0=closed, 1=small, 2=medium, 3=wide
// These hooks are driven directly by TTS PCM playback.
// ============================================================

void elliVisualAudioSpeechBegin();

void elliVisualAudioSpeechLevel(
  uint8_t level
);

void elliVisualAudioSpeechEnd();
