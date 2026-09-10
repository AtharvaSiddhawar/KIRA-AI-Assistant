#pragma once
#include <Arduino.h>

enum SemanticQueryKind {
  SEM_UNKNOWN=0,
  SEM_FACT_REQUEST,
  SEM_DEFINITION,
  SEM_LOOKUP,
  SEM_COMPARISON,
  SEM_RANKING
};

enum SemanticOperator {
  SEM_OP_NONE=0,
  SEM_OP_MAX,
  SEM_OP_MIN,
  SEM_OP_BEST,
  SEM_OP_WORST,
  SEM_OP_COMPARE,
  SEM_OP_EXTREME
};

struct SemanticFrame {
  int kind;
  int op;
  bool question;
  bool subjective;
  bool hasCriterion;
  int confidence;
  String subject;
  String topic;
};

SemanticFrame analyzeSemanticQuery(const String& input);
void printSemanticFrame(const SemanticFrame& frame);

// Runs deterministic/local calendar + current date/time utilities.
// This MUST execute before any AI/web router so local facts cannot be stolen
// by a generic definition or web-search intent.
bool handleDeterministicPreWeb(const String& input);

// Runs before ordinary legacy web routing. It reuses the deterministic layer,
// then handles GENERIC semantic cases such as fresh web-fact requests and
// subjective rankings with no criterion.
bool handleSemanticPreWeb(const String& input);

// Generic concept expansion: original, de-hyphenated, joined, hyphenated,
// and adjacent-token joins. No topic-specific dictionary required.
int buildConceptVariants(String term,String out[],int maxCount);

int semanticWordCount(String q);
String semanticFocusDefinition(String term,String extract);

// Generic evidence scoring used by source adapters.
int semanticEvidenceScore(String query,String title,String candidate);
int semanticRequiredEvidence(String query);
bool semanticCandidateAcceptable(String query,String title,String candidate);
