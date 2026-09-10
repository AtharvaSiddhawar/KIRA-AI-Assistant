#pragma once
#include <Arduino.h>

namespace KiraV1 {

enum IntentType : uint8_t {
  INTENT_UNKNOWN = 0,
  INTENT_DEFINITION,
  INTENT_ENTITY_LOCATION,
  INTENT_CURRENT_WEATHER,
  INTENT_RANKING,
  INTENT_COMPARISON,
  INTENT_FACT_LOOKUP
};

enum QueryOperator : uint8_t {
  OP_NONE = 0,
  OP_MAX,
  OP_MIN,
  OP_BEST,
  OP_WORST,
  OP_COMPARE
};

enum AnswerType : uint8_t {
  ANSWER_TEXT = 0,
  ANSWER_LOCATION,
  ANSWER_WEATHER,
  ANSWER_NUMBER,
  ANSWER_ENTITY
};

struct QueryFrame {
  String raw;
  String normalized;
  IntentType intent;
  QueryOperator op;
  AnswerType expectedAnswer;
  String subject;
  String entity;
  String relation;
  String location;
  String region;
  String metric;
  String criterion;
  bool needsCurrentData;
  bool needsWeb;
  bool ambiguous;
  int confidence;
};

void clearQueryFrame(QueryFrame& frame);
bool analyzeQuery(const String& input, QueryFrame& frame);
const char* intentName(IntentType intent);
const char* operatorName(QueryOperator op);
const char* answerTypeName(AnswerType type);
void printQueryFrame(const QueryFrame& frame);

} // namespace KiraV1
