#include <Arduino.h>
#include "elli_conversation_v3.h"

// Normalizer still lives in kira_brain.ino during modular migration.
String normalizeInput(String s);

namespace {

static const uint8_t CONV3_MAX_TOPICS = 5;
static const uint32_t CONV3_TTL_MS = 15UL * 60UL * 1000UL;

struct Conv3Topic {
  String query;
  String answer;
  uint32_t touchedAt = 0;
};

Conv3Topic topics[CONV3_MAX_TOPICS];
uint8_t topicCount = 0;

String compact(String s, size_t limit){
  s.trim();
  while(s.indexOf("  ") >= 0) s.replace("  ", " ");
  if(s.length() > limit){
    s = s.substring(0, limit);
    s += "...";
  }
  return s;
}

bool expired(const Conv3Topic& t){
  return !t.query.length() || (millis() - t.touchedAt > CONV3_TTL_MS);
}

void purgeExpired(){
  uint8_t write = 0;
  for(uint8_t i = 0; i < topicCount; i++){
    if(expired(topics[i])) continue;
    if(write != i) topics[write] = topics[i];
    write++;
  }
  for(uint8_t i = write; i < topicCount; i++) topics[i] = Conv3Topic();
  topicCount = write;
}

Conv3Topic* active(){
  purgeExpired();
  if(!topicCount) return nullptr;
  return &topics[0];
}


bool hasWord(const String& q, const String& word){
  int from = 0;
  while(from < q.length()){
    int pos = q.indexOf(word, from);
    if(pos < 0) return false;
    bool left = pos == 0 || !isalnum((unsigned char)q[pos - 1]);
    int end = pos + word.length();
    bool right = end >= q.length() || !isalnum((unsigned char)q[end]);
    if(left && right) return true;
    from = pos + 1;
  }
  return false;
}

bool isReferentialContinuation(const String& q){
  const char* const refs[] = {
    "it", "its", "this", "that", "he", "him", "his",
    "she", "her", "hers", "they", "them", "their", "theirs"
  };
  for(size_t i = 0; i < sizeof(refs)/sizeof(refs[0]); i++){
    if(hasWord(q, refs[i])) return true;
  }
  return q.indexOf("which one") >= 0 || q.indexOf("which of them") >= 0;
}

bool startsAny(const String& q, const char* const items[], size_t count, String& tail){
  for(size_t i = 0; i < count; i++){
    String p = items[i];
    if(q.startsWith(p)){
      tail = q.substring(p.length());
      tail.trim();
      return true;
    }
  }
  return false;
}

bool isRepeatRequest(const String& q){
  return q == "repeat that" ||
         q == "repeat it" ||
         q == "say that again" ||
         q == "say it again" ||
         q == "repeat your answer" ||
         q == "repeat your last answer" ||
         q == "what did you just say";
}

bool isPreviousTopicRequest(const String& q){
  return q == "back to the previous topic" ||
         q == "back to previous topic" ||
         q == "go back to the previous topic" ||
         q == "go back to previous topic" ||
         q == "return to the previous topic" ||
         q == "previous topic";
}

bool isSummarizeRequest(const String& q){
  return q == "summarize that" ||
         q == "summarise that" ||
         q == "summarize it" ||
         q == "summarise it" ||
         q == "give me a summary" ||
         q == "short summary";
}

bool isSimpleRequest(const String& q){
  return q == "explain that simply" ||
         q == "explain it simply" ||
         q == "make that simpler" ||
         q == "make it simpler" ||
         q == "explain in simple words" ||
         q == "explain it in simple words";
}

bool isBriefRequest(const String& q){
  return q == "explain briefly" ||
         q == "explain that briefly" ||
         q == "explain it briefly" ||
         q == "make it brief" ||
         q == "short answer";
}

bool isDetailRequest(const String& q){
  return q == "explain in detail" ||
         q == "explain that in detail" ||
         q == "explain it in detail" ||
         q == "go deeper" ||
         q == "go deeper into it" ||
         q == "tell me more" ||
         q == "tell me more about it";
}

bool isExampleRequest(const String& q){
  return q == "give me an example" ||
         q == "give an example" ||
         q == "show me an example" ||
         q == "example please" ||
         q == "give me examples";
}

String topicPrompt(const Conv3Topic& t, const String& instruction){
  String out;
  out.reserve(t.query.length() + instruction.length() + 180);
  out += "Continue the previous knowledge topic. Previous question: '";
  out += t.query;
  out += "'. ";
  out += instruction;
  if(t.answer.length()){
    out += " Previous answer context: '";
    out += compact(t.answer, 260);
    out += "'.";
  }
  return out;
}

String sameIntentEvidenceQuery(const String& previous,const String& target){
  String p=normalizeInput(previous);
  String t=normalizeInput(target);
  if(!t.length()) return p;
  const char* prefixes[]={
    "what is ","what are ","who is ","who was ","where is ","where was ",
    "when is ","when was ","define ","explain ","tell me about "
  };
  for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
    String prefix=prefixes[i];
    if(p.startsWith(prefix)) return prefix+t;
  }
  return t;
}

void moveTopicToFront(uint8_t index){
  if(index >= topicCount || index == 0) return;
  Conv3Topic chosen = topics[index];
  for(int i = index; i > 0; i--) topics[i] = topics[i - 1];
  topics[0] = chosen;
  topics[0].touchedAt = millis();
}

void pushTopic(const String& query, const String& answer){
  String q = compact(query, 220);
  String a = compact(answer, 360);
  if(!q.length()) return;

  // Same topic/query: refresh rather than duplicate.
  for(uint8_t i = 0; i < topicCount; i++){
    if(normalizeInput(topics[i].query) == normalizeInput(q)){
      topics[i].answer = a;
      topics[i].touchedAt = millis();
      moveTopicToFront(i);
      return;
    }
  }

  uint8_t limit = topicCount < CONV3_MAX_TOPICS ? topicCount : CONV3_MAX_TOPICS - 1;
  for(int i = limit; i > 0; i--) topics[i] = topics[i - 1];
  topics[0].query = q;
  topics[0].answer = a;
  topics[0].touchedAt = millis();
  if(topicCount < CONV3_MAX_TOPICS) topicCount++;
}

} // namespace

void elliConversationV3Begin(){
  elliConversationV3Clear();
  Serial.println("[DIALOGUE V3] Topic continuity ready (RAM-only, 5 topics / 15 min)." );
}

void elliConversationV3Clear(){
  for(uint8_t i = 0; i < CONV3_MAX_TOPICS; i++) topics[i] = Conv3Topic();
  topicCount = 0;
}

bool elliConversationV3HasTopic(){
  return active() != nullptr;
}

String elliConversationV3ActiveQuery(){
  Conv3Topic* t = active();
  return t ? t->query : String();
}

ElliConversationV3Result elliConversationV3Preprocess(const String& input){
  ElliConversationV3Result result;
  String q = normalizeInput(input);
  Conv3Topic* t = active();

  if(isRepeatRequest(q)){
    result.action = ELLI_CONV3_LOCAL_REPLY;
    result.localReply = (t && t->answer.length())
      ? t->answer
      : "I don't have a recent knowledge answer to repeat yet.";
    return result;
  }

  if(isPreviousTopicRequest(q)){
    purgeExpired();
    if(topicCount < 2){
      result.action = ELLI_CONV3_LOCAL_REPLY;
      result.localReply = "I don't have an older knowledge topic in short-term context yet.";
      return result;
    }

    moveTopicToFront(1);
    t = active();
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = topicPrompt(*t, "Continue this topic from where we left it and give one useful next piece of information.");
    result.evidenceQuery = t->query;
    return result;
  }

  if(!t) return result;

  if(isSummarizeRequest(q)){
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = topicPrompt(*t, "Summarize the topic clearly and concisely without adding unrelated material.");
    result.evidenceQuery = t->query;
    return result;
  }

  if(isSimpleRequest(q)){
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = topicPrompt(*t, "Explain it again in simple beginner-friendly language with minimal jargon.");
    result.evidenceQuery = t->query;
    return result;
  }

  if(isBriefRequest(q)){
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = topicPrompt(*t, "Give a brief direct explanation in about two or three sentences.");
    result.evidenceQuery = t->query;
    return result;
  }

  if(isDetailRequest(q)){
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = topicPrompt(*t, "Explain it in more detail, add useful context, and avoid merely repeating the previous wording.");
    result.evidenceQuery = t->query;
    return result;
  }

  if(isExampleRequest(q)){
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = topicPrompt(*t, "Give one or two concrete examples and explain how each example connects to the topic.");
    result.evidenceQuery = t->query;
    return result;
  }

  String tail;
  const char* const correctionPrefixes[] = {
    "no i meant ", "no, i meant ", "i meant ", "actually i meant ",
    "sorry i meant ", "not that i meant "
  };

  if(startsAny(q, correctionPrefixes, sizeof(correctionPrefixes)/sizeof(correctionPrefixes[0]), tail) && tail.length()){
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = "Correct the previous request. Previous question: '" + t->query + "'. The user says they meant '" + tail + "'. Answer the corrected version, preserving the previous question's intent where sensible.";
    result.evidenceQuery = sameIntentEvidenceQuery(t->query,tail);
    return result;
  }

  const char* const whatAboutPrefixes[] = {"what about ", "and what about ", "how about "};
  if(startsAny(q, whatAboutPrefixes, sizeof(whatAboutPrefixes)/sizeof(whatAboutPrefixes[0]), tail) && tail.length()){
    result.action = ELLI_CONV3_REWRITE_KNOWLEDGE;
    result.rewrittenQuery = "Use the same intent and comparison/fact pattern as the previous question '" + t->query + "', but now apply it to '" + tail + "'.";
    result.evidenceQuery = sameIntentEvidenceQuery(t->query,tail);
    return result;
  }

  return result;
}

void elliConversationV3ObserveKnowledge(
  const String& userQuery,
  const String& executedQuery,
  const String& assistantAnswer
){
  String u = normalizeInput(userQuery);
  if(!assistantAnswer.length()) return;

  // Continuation phrases should refresh the active topic instead of becoming
  // useless topic labels like "summarize that".
  bool continuation =
    isSummarizeRequest(u) || isSimpleRequest(u) || isBriefRequest(u) ||
    isDetailRequest(u) || isExampleRequest(u) || isPreviousTopicRequest(u) ||
    u.startsWith("what about ") || u.startsWith("and what about ") ||
    u.startsWith("how about ") || u.startsWith("no i meant ") ||
    u.startsWith("i meant ") || u.startsWith("actually i meant ") ||
    isReferentialContinuation(u) ||
    u.indexOf(" conversation context:") >= 0;

  Conv3Topic* t = active();

  if(continuation && t){
    t->answer = compact(assistantAnswer, 360);
    t->touchedAt = millis();

    // Corrections / "what about" change the effective subject, so preserve
    // the executed request as the refreshed active query when it is compact.
    if((u.indexOf("meant ") >= 0 || u.startsWith("what about ") || u.startsWith("how about ")) && executedQuery.length()){
      t->query = compact(executedQuery, 220);
    }
    return;
  }

  // A normal new knowledge question becomes the active topic.
  String topicQuery = userQuery.length() ? userQuery : executedQuery;
  pushTopic(topicQuery, assistantAnswer);
}

String elliConversationV3Status(){
  purgeExpired();
  String out = "Conversation V3: ";
  out += String(topicCount);
  out += " recent knowledge topic";
  if(topicCount != 1) out += "s";
  out += ".";

  if(topicCount){
    out += " Active topic: ";
    out += topics[0].query;
    out += ".";
  }
  return out;
}
