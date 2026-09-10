#include <Arduino.h>
#include "kira_answer_verify.h"

namespace {

static int capConfidence(int value,int cap){ return value < cap ? value : cap; }

KiraVerificationReport lastReport;
bool haveLastReport = false;

static String verifyNormalize(String s){
  s.toLowerCase();
  s.trim();
  while(s.indexOf("  ") >= 0) s.replace("  ", " ");
  return s;
}

static bool containsWordOrPhrase(const String& normalized, const char* phrase){
  String p = phrase;
  if(p.indexOf(' ') >= 0) return normalized.indexOf(p) >= 0;
  String padded = " " + normalized + " ";
  return padded.indexOf(" " + p + " ") >= 0;
}

static bool answerSignalsUncertainty(String answer){
  answer = verifyNormalize(answer);
  if(answer.length() < 8) return true;

  const char* phrases[] = {
    "i don't know",
    "i do not know",
    "cannot verify",
    "can't verify",
    "could not verify",
    "couldn't verify",
    "not enough information",
    "insufficient information",
    "unable to determine",
    "unable to verify",
    "no reliable evidence"
  };

  for(size_t i = 0; i < sizeof(phrases)/sizeof(phrases[0]); i++){
    if(answer.indexOf(phrases[i]) >= 0) return true;
  }
  return false;
}

} // namespace

const char* kiraVerificationLevelName(KiraVerificationLevel level){
  switch(level){
    case KIRA_VERIFY_AI_ONLY: return "AI_ONLY";
    case KIRA_VERIFY_SUPPORTED: return "SUPPORTED";
    case KIRA_VERIFY_CROSSCHECKED: return "CROSSCHECKED";
    case KIRA_VERIFY_CURRENT_LIMITED: return "CURRENT_LIMITED";
    case KIRA_VERIFY_LOW_CONFIDENCE: return "LOW_CONFIDENCE";
    default: return "NONE";
  }
}

bool kiraQuestionNeedsFreshVerification(const String& question){
  String q = verifyNormalize(question);

  const char* fresh[] = {
    "latest", "today", "currently", "current", "right now", "now",
    "this week", "this month", "this year", "live", "recent",
    "breaking", "price today", "current price", "weather", "forecast",
    "score", "standings", "stock price", "exchange rate", "2026"
  };

  for(size_t i = 0; i < sizeof(fresh)/sizeof(fresh[0]); i++){
    if(containsWordOrPhrase(q, fresh[i])) return true;
  }
  return false;
}

KiraVerificationReport kiraVerifyAnswer(
  const String& question,
  const String& answer,
  int providerConfidence,
  uint8_t evidenceSources
){
  KiraVerificationReport r;
  r.providerConfidence = constrain(providerConfidence, 0, 100);
  r.adjustedConfidence = r.providerConfidence;
  r.evidenceSources = evidenceSources;
  r.freshnessSensitive = kiraQuestionNeedsFreshVerification(question);

  if(answerSignalsUncertainty(answer)){
    r.level = KIRA_VERIFY_LOW_CONFIDENCE;
    r.adjustedConfidence = capConfidence(r.adjustedConfidence, 45);
    r.cacheable = false;
    r.verified = false;
    r.label = "low-confidence answer";
    r.reason = "The provider answer itself signals uncertainty or is too incomplete to verify.";
    return r;
  }

  if(r.freshnessSensitive){
    r.level = KIRA_VERIFY_CURRENT_LIMITED;
    // DuckDuckGo Instant Answer + Wikipedia are useful evidence, but they are
    // not guaranteed live/current feeds. Never overstate freshness.
    r.adjustedConfidence = capConfidence(r.adjustedConfidence, evidenceSources >= 2 ? 68 : 58);
    r.cacheable = false;
    r.verified = false;
    r.label = "current-data verification limited";
    r.reason = "The question is freshness-sensitive and KIRA's free evidence sources are not guaranteed live.";
    return r;
  }

  if(evidenceSources >= 2){
    r.level = KIRA_VERIFY_CROSSCHECKED;
    r.adjustedConfidence = capConfidence(r.adjustedConfidence, 92);
    r.verified = r.adjustedConfidence >= 60;
    r.cacheable = r.adjustedConfidence >= 70;
    r.label = "cross-checked";
    r.reason = "At least two independent evidence families supported the research turn.";
    return r;
  }

  if(evidenceSources == 1){
    r.level = KIRA_VERIFY_SUPPORTED;
    r.adjustedConfidence = capConfidence(r.adjustedConfidence, 82);
    r.verified = r.adjustedConfidence >= 60;
    r.cacheable = r.adjustedConfidence >= 72;
    r.label = "single-source supported";
    r.reason = "One external evidence family supported the answer; confidence is capped accordingly.";
    return r;
  }

  r.level = KIRA_VERIFY_AI_ONLY;
  r.adjustedConfidence = capConfidence(r.adjustedConfidence, 65);
  r.cacheable = false;
  r.verified = false;
  r.label = "AI reasoning only";
  r.reason = "No external evidence was retrieved, so KIRA does not treat the answer as verified knowledge.";
  return r;
}

void kiraRememberVerification(const KiraVerificationReport& report){
  lastReport = report;
  haveLastReport = true;
}

String kiraLastVerificationSummary(){
  if(!haveLastReport) return "No AI/web answer has been verification-scored yet.";

  String out = "Verification ";
  out += kiraVerificationLevelName(lastReport.level);
  out += ": confidence ";
  out += String(lastReport.adjustedConfidence);
  out += "/100, evidence sources ";
  out += String(lastReport.evidenceSources);
  out += lastReport.cacheable ? ", cacheable." : ", not cacheable.";
  return out;
}

void kiraPrintLastVerification(){
  Serial.println();
  Serial.println("========== ANSWER VERIFICATION ==========");
  if(!haveLastReport){
    Serial.println("No verification report yet.");
  }else{
    Serial.print("Level              : "); Serial.println(kiraVerificationLevelName(lastReport.level));
    Serial.print("Provider confidence: "); Serial.println(lastReport.providerConfidence);
    Serial.print("Adjusted confidence: "); Serial.println(lastReport.adjustedConfidence);
    Serial.print("Evidence sources   : "); Serial.println(lastReport.evidenceSources);
    Serial.print("Freshness-sensitive: "); Serial.println(lastReport.freshnessSensitive ? "YES" : "NO");
    Serial.print("Verified           : "); Serial.println(lastReport.verified ? "YES" : "NO");
    Serial.print("Cacheable          : "); Serial.println(lastReport.cacheable ? "YES" : "NO");
    Serial.print("Reason             : "); Serial.println(lastReport.reason);
  }
  Serial.println("=========================================");
}

void kiraVerificationBegin(){
  lastReport = KiraVerificationReport();
  haveLastReport = false;
  Serial.println("[VERIFY V1] Cross-source confidence and cache gate ready.");
}
