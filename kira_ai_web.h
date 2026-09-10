#pragma once
#include <Arduino.h>
#include "elli_query_frame.h"
#include "elli_entity_resolver.h"
#include "kira_answer_verify.h"

namespace KiraV1 {

struct AiWebResult {
  bool success=false;

  // The AI may suggest ambiguity, but KIRA makes the final local decision.
  bool needsClarification=false;

  String answer;
  String source;
  int confidence=0;

  // V1.5 local verification metadata. The provider may answer, but KIRA
  // independently decides whether the result is evidence-supported enough
  // to cache as verified knowledge.
  KiraVerificationLevel verificationLevel=KIRA_VERIFY_NONE;
  uint8_t evidenceSources=0;
  bool freshnessSensitive=false;
  bool verified=false;
  bool cacheable=false;

  int candidateCount=0;
  int firstPlausibility=0;
  int secondPlausibility=0;

  Candidate first;
  Candidate second;
};

bool aiWebConfigured();
bool aiProvidersUsable();
void printAiProviderStatus();
String aiProviderStatusCompact();
bool askGroqWeb(
  const QueryFrame& frame,
  const String& userQuestion,
  AiWebResult& result,
  const String& clarifiedEntity="",
  bool skipFreeEvidence=false,
  const String& evidenceQueryOverride=""
);

bool aiWebEnvelopeParserSelfTest();

} // namespace KiraV1
