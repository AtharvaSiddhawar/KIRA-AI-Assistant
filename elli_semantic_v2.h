#pragma once
#include <Arduino.h>

#include "elli_nlu.h"

// =====================================================
// ELLI SEMANTIC FRAME V2
// =====================================================
//
// NLU answers:
//   "What kind of sentence is this?"
//
// Semantic Frame V2 answers:
//   "What does this sentence mean?"
//
// The frame is intentionally open-vocabulary.
//
// It does NOT require one exact rule for every possible topic.
// Known semantic classes receive stable relation kinds, while unknown
// relations fall back to a generated relation label.
//
// Example:
//   "my favorite game is minecraft"
//
// can become:
//   subject        = USER
//   relation kind  = ATTRIBUTE
//   relation label = FAVORITE_GAME
//   value          = minecraft
//
// even if "game" was never a special firmware topic.
// =====================================================

enum ElliSemanticSubjectKind {
  ELLI_SEM_SUBJECT_UNKNOWN = 0,
  ELLI_SEM_SUBJECT_USER,
  ELLI_SEM_SUBJECT_ELLI,
  ELLI_SEM_SUBJECT_GROUP,
  ELLI_SEM_SUBJECT_ENTITY
};

enum ElliSemanticRelationKind {
  ELLI_SEM_REL_UNKNOWN = 0,

  ELLI_SEM_REL_ATTRIBUTE,
  ELLI_SEM_REL_IDENTITY,
  ELLI_SEM_REL_STATE,

  ELLI_SEM_REL_EDUCATION_LEVEL,
  ELLI_SEM_REL_EDUCATION_ACTIVITY,

  ELLI_SEM_REL_AGE,
  ELLI_SEM_REL_RELATIONSHIP,
  ELLI_SEM_REL_PREFERENCE,
  ELLI_SEM_REL_POSSESSION,

  ELLI_SEM_REL_RESIDENCE,
  ELLI_SEM_REL_LOCATION,
  ELLI_SEM_REL_MEMBERSHIP,

  ELLI_SEM_REL_OCCUPATION,
  ELLI_SEM_REL_LANGUAGE,
  ELLI_SEM_REL_PROJECT,

  ELLI_SEM_REL_ACTIVITY,
  ELLI_SEM_REL_CAPABILITY,
  ELLI_SEM_REL_GOAL,
  ELLI_SEM_REL_INTENTION,

  ELLI_SEM_REL_QUANTITY,
  ELLI_SEM_REL_TIME,

  ELLI_SEM_REL_CUSTOM
};

enum ElliSemanticCertainty {
  ELLI_SEM_CERT_UNKNOWN = 0,
  ELLI_SEM_CERT_ASSERTED,
  ELLI_SEM_CERT_NEGATED,
  ELLI_SEM_CERT_POSSIBLE,
  ELLI_SEM_CERT_PROBABLE,
  ELLI_SEM_CERT_CONDITIONAL,
  ELLI_SEM_CERT_QUESTIONED
};

enum ElliSemanticQueryTarget {
  ELLI_SEM_QUERY_NONE = 0,
  ELLI_SEM_QUERY_VALUE,
  ELLI_SEM_QUERY_PERSON,
  ELLI_SEM_QUERY_PLACE,
  ELLI_SEM_QUERY_TIME,
  ELLI_SEM_QUERY_REASON,
  ELLI_SEM_QUERY_METHOD,
  ELLI_SEM_QUERY_CHOICE,
  ELLI_SEM_QUERY_QUANTITY,
  ELLI_SEM_QUERY_BOOLEAN
};

struct ElliSemanticFrame {

  String raw;
  String normalized;

  // Surface-level meaning.
  String subject;
  String relationLabel;
  String relationSurface;
  String value;
  String object;

  // Optional semantic modifiers.
  String timeText;
  String locationText;
  String questionWord;
  String mainVerb;
  String qualifier;

  ElliSemanticSubjectKind subjectKind=
    ELLI_SEM_SUBJECT_UNKNOWN;

  ElliSemanticRelationKind relationKind=
    ELLI_SEM_REL_UNKNOWN;

  ElliSemanticCertainty certainty=
    ELLI_SEM_CERT_UNKNOWN;

  ElliSemanticQueryTarget queryTarget=
    ELLI_SEM_QUERY_NONE;

  // Grammar copied from Universal NLU.
  ElliTense tense=
    ELLI_TENSE_UNKNOWN;

  ElliAspect aspect=
    ELLI_ASPECT_SIMPLE;

  bool valid=false;
  bool question=false;
  bool personal=false;
  bool negated=false;
  bool modal=false;
  bool compound=false;
  bool pluralValue=false;

  uint8_t confidence=0;
};


// Build meaning after grammar.
ElliSemanticFrame elliBuildSemanticFrame(String input);

// Human-readable helpers.
const char* elliSemanticSubjectName(ElliSemanticSubjectKind kind);
const char* elliSemanticRelationName(ElliSemanticRelationKind kind);
const char* elliSemanticCertaintyName(ElliSemanticCertainty certainty);
const char* elliSemanticQueryTargetName(ElliSemanticQueryTarget target);

// Debug command target:
//   semantic sense <sentence>
void printElliSemanticFrame(String input);
