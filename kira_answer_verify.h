#pragma once
#include <Arduino.h>

enum KiraVerificationLevel : uint8_t {
  KIRA_VERIFY_NONE = 0,
  KIRA_VERIFY_AI_ONLY,
  KIRA_VERIFY_SUPPORTED,
  KIRA_VERIFY_CROSSCHECKED,
  KIRA_VERIFY_CURRENT_LIMITED,
  KIRA_VERIFY_LOW_CONFIDENCE
};

struct KiraVerificationReport {
  KiraVerificationLevel level = KIRA_VERIFY_NONE;
  int providerConfidence = 0;
  int adjustedConfidence = 0;
  uint8_t evidenceSources = 0;
  bool freshnessSensitive = false;
  bool cacheable = false;
  bool verified = false;
  String label;
  String reason;
};

void kiraVerificationBegin();
KiraVerificationReport kiraVerifyAnswer(
  const String& question,
  const String& answer,
  int providerConfidence,
  uint8_t evidenceSources
);
void kiraRememberVerification(const KiraVerificationReport& report);
String kiraLastVerificationSummary();
void kiraPrintLastVerification();
const char* kiraVerificationLevelName(KiraVerificationLevel level);
bool kiraQuestionNeedsFreshVerification(const String& question);
