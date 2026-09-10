#pragma once
#include <Arduino.h>
#include "elli_query_frame.h"
#include "elli_entity_resolver.h"

namespace KiraV1 {

enum ContextKind:uint8_t { CONTEXT_NONE=0, CONTEXT_ENTITY_CHOICE, CONTEXT_MISSING_LOCATION, CONTEXT_MISSING_REGION, CONTEXT_MISSING_CRITERION };
enum ContextResult:uint8_t { CONTEXT_NO_PENDING=0, CONTEXT_RESOLVED, CONTEXT_NEEDS_MORE, CONTEXT_CANCELLED, CONTEXT_EXPIRED };

struct PendingContext {
  bool active=false;
  ContextKind kind=CONTEXT_NONE;
  QueryFrame originalFrame;
  Candidate first;
  Candidate second;
  String prompt;
  unsigned long createdAt=0;
  uint8_t attempts=0;
};

struct ContextResolution {
  ContextResult result=CONTEXT_NO_PENDING;
  QueryFrame frame;
  Candidate chosen;
  bool hasChosenCandidate=false;
  String message;
};

class ConversationContext {
public:
  ConversationContext();
  void clear();
  bool active() const;
  bool live();
  ContextKind kind() const;
  void beginEntityChoice(const QueryFrame& frame,const Candidate& first,const Candidate& second);
  void beginMissingLocation(const QueryFrame& frame);
  void beginMissingRegion(const QueryFrame& frame);
  void beginMissingCriterion(const QueryFrame& frame);
  String prompt() const;
  ContextResolution consume(const String& reply);
private:
  PendingContext pending_;
  bool expired() const;
  static bool isCancelReply(const String& reply);
  static int matchCandidateReply(const String& reply,const Candidate& candidate);
  static String cleanFollowUp(const String& reply);
};

} // namespace KiraV1
