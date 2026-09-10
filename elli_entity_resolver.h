#pragma once
#include <Arduino.h>
#include "elli_query_frame.h"

namespace KiraV1 {

enum EntityType : uint8_t {
  ENTITY_UNKNOWN=0, ENTITY_PLACE, ENTITY_COUNTRY, ENTITY_CITY, ENTITY_MONUMENT,
  ENTITY_HOTEL, ENTITY_PERSON, ENTITY_ORGANIZATION, ENTITY_LIST_PAGE, ENTITY_DISAMBIGUATION
};

struct Candidate {
  String title;
  String description;
  String aliases;
  String source;
  EntityType type=ENTITY_UNKNOWN;
  int retrievalScore=0;
  int semanticScore=0;
  int finalScore=0;
};

enum ResolutionAction : uint8_t { RESOLVE_NONE=0, RESOLVE_ACCEPT_TOP, RESOLVE_ASK_TOP_TWO, RESOLVE_ASK_REPHRASE };

struct ResolutionDecision {
  ResolutionAction action=RESOLVE_NONE;
  int firstIndex=-1;
  int secondIndex=-1;
  int firstScore=0;
  int secondScore=0;
  int gap=0;
};

EntityType inferEntityType(const Candidate& candidate);
EntityType entityTypeFromText(const String& text);
const char* entityTypeName(EntityType type);
int scoreCandidate(const QueryFrame& frame,Candidate& candidate);
ResolutionDecision resolveCandidates(const QueryFrame& frame,Candidate candidates[],int count);
String candidateLabel(const Candidate& candidate);

} // namespace KiraV1
