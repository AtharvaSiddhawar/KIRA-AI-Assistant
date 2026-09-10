#pragma once
#include <Arduino.h>

// =====================================================
// KIRA INTENT ARBITER V1
// =====================================================
//
// PURPOSE
// -------
// Current KIRA has several mature language layers:
//
//   elli_intent      -> legacy/safety intent
//   elli_nlu         -> grammar / tense / sentence kind
//   Semantic Frame V2-> meaning / relation / query target
//
// Intent Arbiter V1 FUSES those signals into one score table.
//
// IMPORTANT:
// V1 launches in SHADOW MODE.
//
// It does NOT control routing yet.
// It only:
//   1. scores every major route class
//   2. prints the proposed winner
//   3. compares it with the route that actually ran
//   4. marks close competitions as ambiguous
//
// After the regression suite is clean, a future release can enable
// controlled takeover class-by-class.
//
// This protects all current working progress.
// =====================================================


// -----------------------------------------------------
// FEATURE FLAGS
// -----------------------------------------------------

#ifndef KIRA_INTENT_ARBITER_SHADOW
#define KIRA_INTENT_ARBITER_SHADOW 1
#endif

// DO NOT enable in V1.
// Reserved for a later regression-gated takeover release.
#ifndef KIRA_INTENT_ARBITER_TAKEOVER
#define KIRA_INTENT_ARBITER_TAKEOVER 0
#endif


enum ElliArbiterClass {

  ELLI_ARB_NONE = 0,

  ELLI_ARB_CLOCK,
  ELLI_ARB_DEVICE,
  ELLI_ARB_TASK,
  ELLI_ARB_ROUTINE,

  ELLI_ARB_PERSONAL_MEMORY,
  ELLI_ARB_SUPPORT,

  ELLI_ARB_KNOWLEDGE,
  ELLI_ARB_CHAT,
  ELLI_ARB_PERSONAL_CONVERSATION,
  ELLI_ARB_STATEMENT,

  ELLI_ARB_CLASS_COUNT
};


struct ElliArbiterDecision {

  uint8_t score[
    ELLI_ARB_CLASS_COUNT
  ]={0};

  ElliArbiterClass winner=
    ELLI_ARB_NONE;

  ElliArbiterClass runnerUp=
    ELLI_ARB_NONE;

  uint8_t winnerScore=0;
  uint8_t runnerUpScore=0;
  uint8_t gap=0;

  bool ambiguous=false;
  bool decisive=false;

  int legacyIntent=0;

  uint8_t nluConfidence=0;
  uint8_t semanticConfidence=0;
};


// Main fusion function.
ElliArbiterDecision elliArbitrate(const String& input);


// Human-readable names.
const char* elliArbiterClassName(ElliArbiterClass cls);


// Compact per-turn shadow telemetry.
void printElliArbiterShadow(
  const ElliArbiterDecision& decision
);


// Full debug command:
//
//   arbiter sense <sentence>
//
void printElliArbiterDecision(
  const String& input
);


// Compare the arbiter's proposed class with the CURRENT working router.
// This is telemetry only; it never changes execution.
void elliArbiterCompareRoute(
  const ElliArbiterDecision& decision,
  const String& actualRoute
);
