#pragma once
#include <Arduino.h>

// =====================================================
// ELLI UNIVERSAL LANGUAGE FRAME
// =====================================================
//
// This layer analyzes sentence STRUCTURE rather than relying on
// topic-specific phrase lists. It is intentionally lightweight enough
// for ESP32-S3 but covers the grammar signals KIRA needs for routing:
//
//   question / command / statement / fragment
//   first/second/third-person references
//   present / past / future
//   simple / continuous / perfect / perfect-continuous
//   negation / modality / conjunctions / compound clauses
//   stable-vs-transient personal statements
//
// It does NOT try to be a complete English parser. Open vocabulary is
// preserved for the AI/knowledge layer; this local frame decides how the
// utterance should be routed safely.
// =====================================================

enum ElliSentenceKind {
  ELLI_SENTENCE_UNKNOWN = 0,
  ELLI_SENTENCE_QUESTION,
  ELLI_SENTENCE_COMMAND,
  ELLI_SENTENCE_STATEMENT,
  ELLI_SENTENCE_FRAGMENT
};

enum ElliTense {
  ELLI_TENSE_UNKNOWN = 0,
  ELLI_TENSE_PRESENT,
  ELLI_TENSE_PAST,
  ELLI_TENSE_FUTURE
};

enum ElliAspect {
  ELLI_ASPECT_SIMPLE = 0,
  ELLI_ASPECT_CONTINUOUS,
  ELLI_ASPECT_PERFECT,
  ELLI_ASPECT_PERFECT_CONTINUOUS
};

struct ElliNLUFrame {
  String normalized;
  String firstWord;
  String conjunction;

  ElliSentenceKind kind=ELLI_SENTENCE_UNKNOWN;
  ElliTense tense=ELLI_TENSE_UNKNOWN;
  ElliAspect aspect=ELLI_ASPECT_SIMPLE;

  uint8_t wordCount=0;
  uint8_t confidence=0;

  bool question=false;
  bool command=false;
  bool statement=false;
  bool fragment=false;

  bool firstPerson=false;
  bool secondPerson=false;
  bool thirdPerson=false;
  bool personal=false;

  bool negated=false;
  bool modal=false;
  bool compound=false;

  bool stablePersonal=false;
  bool transientPersonal=false;
};

ElliNLUFrame elliAnalyzeNLU(String input);

bool elliLooksLikeQuestionSyntax(String input);
bool elliLooksLikeCommandSyntax(String input);
bool elliLooksLikePersonalStatementSyntax(String input);
bool elliLooksLikeStablePersonalFactSyntax(String input);
bool elliLooksLikeTransientPersonalStatementSyntax(String input);

// Convert a user's first-person fact into Elli's second-person wording.
// Example: "i am in 10th grade" -> "you are in 10th grade"
String elliPerspectiveToUser(String input);

// Natural local sentence-forming helpers.
String elliNaturalPersonalAcknowledgement(String input);
String elliNaturalStatementAcknowledgement(String input);

// Natural long-term-memory sentence forming.
String elliNaturalMemorySavedReply(String input,bool explicitSave);
String elliNaturalMemoryKnownReply(String input);
String elliNaturalMemoryUnsavedReply(String input);

const char* elliSentenceKindName(ElliSentenceKind kind);
const char* elliTenseName(ElliTense tense);
const char* elliAspectName(ElliAspect aspect);

void printElliNLUFrame(String input);