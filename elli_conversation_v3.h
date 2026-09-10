#pragma once
#include <Arduino.h>

// =====================================================
// ELLI CONVERSATION V3
// =====================================================
// RAM-only topic continuity layered on top of the existing V1/V2
// clarification engines. It never writes personal information to flash.
//
// Adds:
//   - repeat/summarize/simple/detail/example follow-ups
//   - "no, I meant ..." corrections
//   - "what about ..." same-question topic switching
//   - previous-topic return
//   - compact recent topic history
// =====================================================

enum ElliConversationV3Action : uint8_t {
  ELLI_CONV3_NONE = 0,
  ELLI_CONV3_REWRITE_KNOWLEDGE,
  ELLI_CONV3_LOCAL_REPLY
};

struct ElliConversationV3Result {
  ElliConversationV3Action action = ELLI_CONV3_NONE;
  String rewrittenQuery;
  String evidenceQuery;
  String localReply;
};

void elliConversationV3Begin();
void elliConversationV3Clear();

// Called before normal knowledge routing. It only consumes unmistakable
// continuation phrases; unrelated new questions are left untouched.
ElliConversationV3Result elliConversationV3Preprocess(const String& input);

// Called after a successful knowledge answer.
void elliConversationV3ObserveKnowledge(
  const String& userQuery,
  const String& executedQuery,
  const String& assistantAnswer
);

bool elliConversationV3HasTopic();
String elliConversationV3ActiveQuery();
String elliConversationV3Status();
