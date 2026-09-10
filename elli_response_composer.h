#pragma once
#include <Arduino.h>

#include "elli_semantic_v2.h"

// =====================================================
// ELLI RESPONSE COMPOSER V1
// =====================================================
//
// Semantic Frame V2 decides the meaning.
// Response Composer V1 decides how Elli should SAY it.
//
// The composer owns:
//   perspective
//   tense
//   contractions
//   punctuation
//   direct-vs-memory wording
//   lightweight tone
//   response variation
//
// It always has a fallback to the proven V7.1 NLU response system.
// =====================================================

enum ElliResponsePurpose {
  ELLI_RESPONSE_PERSONAL_ACK = 0,
  ELLI_RESPONSE_MEMORY_SAVED,
  ELLI_RESPONSE_MEMORY_KNOWN,
  ELLI_RESPONSE_MEMORY_UNSAVED,
  ELLI_RESPONSE_MEMORY_RECALL,
  ELLI_RESPONSE_DEBUG
};

enum ElliResponseTone {
  ELLI_TONE_NEUTRAL = 0,
  ELLI_TONE_WARM,
  ELLI_TONE_EMPATHETIC,
  ELLI_TONE_POSITIVE,
  ELLI_TONE_DIRECT
};

struct ElliResponsePlan {

  ElliResponsePurpose purpose=
    ELLI_RESPONSE_PERSONAL_ACK;

  ElliResponseTone tone=
    ELLI_TONE_NEUTRAL;

  String prefix;
  String core;
  String suffix;
  String followup;

  bool useFollowup=false;
  uint8_t confidence=0;
};


// Core proposition produced from semantic meaning.
// Example:
//   "i was in 9th grade last year"
//   -> "You were in 9th grade last year."
String elliSemanticProposition(const ElliSemanticFrame& frame);


// High-level response functions used by the memory/conversation layer.
String elliComposePersonalSemantic(String input);

String elliComposeMemorySavedSemantic(
  String input,
  bool explicitSave
);

String elliComposeMemoryKnownSemantic(String input);

String elliComposeMemoryUnsavedSemantic(String input);

String elliComposeMemoryRecallSemantic(
  String query,
  String firstFact,
  String secondFact="",
  String thirdFact=""
);


// Debug:
//   response sense <sentence>
void printElliResponsePlan(String input);
