#pragma once
#include <Arduino.h>

// =====================================================
//               LOCAL TEMPORAL REQUESTS
// =====================================================

enum TemporalRequestKind {
  TEMPORAL_NONE = 0,
  TEMPORAL_TIME,
  TEMPORAL_DATE,
  TEMPORAL_DATE_TIME
};

// =====================================================
//                 UTTERANCE INTENTS
// =====================================================

enum ElliUtteranceIntent {
  UTT_CHAT      = 1,
  UTT_PROFILE   = 2,
  UTT_TASK      = 3,
  UTT_ROUTINE   = 4,
  UTT_DEVICE    = 5,
  UTT_QUESTION  = 6,
  UTT_SUPPORT   = 7,
  UTT_CLOCK     = 8,
  UTT_STATEMENT = 9
};

TemporalRequestKind detectLocalTemporalRequest(String q);

const char* utteranceIntentName(int intent);
int detectUtteranceIntent(String q);
void printUtteranceSense(String q);