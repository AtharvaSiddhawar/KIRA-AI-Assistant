#include <Arduino.h>
#include "kira_language_tools.h"
#include "elli_query_frame.h"
#include "kira_ai_web.h"
#include "kira_v1_router.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

static bool knownLanguage(const String& lang){
  const char* known[]={
    "english","hindi","marathi","german","dutch","french","spanish",
    "italian","portuguese","japanese","korean","chinese","mandarin",
    "tamil","telugu","bengali","gujarati","punjabi","urdu"
  };
  String l=normalizeInput(lang);
  for(size_t i=0;i<sizeof(known)/sizeof(known[0]);i++) if(l==known[i]) return true;
  return false;
}

static bool parseTranslation(String q,String& text,String& target){
  q=normalizeInput(q);
  text=""; target="";

  if(q.startsWith("translate ")){
    String rest=q.substring(10);
    int p=rest.lastIndexOf(" into ");
    int advance=6;
    if(p<0){ p=rest.lastIndexOf(" to "); advance=4; }
    if(p<0) return false;
    text=rest.substring(0,p); target=rest.substring(p+advance);
  }
  else if(q.startsWith("say ")){
    String rest=q.substring(4);
    int p=rest.lastIndexOf(" in ");
    if(p<0) return false;
    text=rest.substring(0,p); target=rest.substring(p+4);
  }
  else return false;

  text.trim(); target.trim();
  return text.length() && knownLanguage(target);
}

static void makeGenericFrame(const String& raw,const String& subject,const String& relation,KiraV1::QueryFrame& frame){
  KiraV1::clearQueryFrame(frame);
  frame.raw=raw;
  frame.normalized=normalizeInput(raw);
  frame.intent=KiraV1::INTENT_FACT_LOOKUP;
  frame.op=KiraV1::OP_NONE;
  frame.expectedAnswer=KiraV1::ANSWER_TEXT;
  frame.subject=subject;
  frame.entity=subject;
  frame.relation=relation;
  frame.needsCurrentData=false;
  frame.needsWeb=true;
  frame.ambiguous=false;
  frame.confidence=100;
}

static bool runProviderRequest(const KiraV1::QueryFrame& frame,const String& prompt){
  if(!KiraV1::aiWebConfigured()){
    elliSay("That language tool needs an online AI provider in this build, and none is available right now.");
    return true;
  }

  KiraV1::AiWebResult result;
  if(!KiraV1::askGroqWeb(frame,prompt,result,"",true)){
    elliSay("I couldn't complete that language request through the available providers right now.");
    return true;
  }

  if(result.needsClarification){
    elliSay("I need a little more context before I can answer that language request accurately.");
    return true;
  }

  if(result.answer.length()) elliSay(result.answer);
  else elliSay("The provider responded, but I didn't get a usable language result.");
  return true;
}

static String dictionaryTerm(String q){
  q=normalizeInput(q);
  const char* prefixes[]={"define ","definition of ","synonym of ","synonyms of ","antonym of ","antonyms of "};
  for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
    String p=prefixes[i];
    if(q.startsWith(p)){ q.remove(0,p.length()); q.trim(); return q; }
  }

  if(q.startsWith("what does ") && q.endsWith(" mean")){
    q=q.substring(10,q.length()-5); q.trim(); return q;
  }
  return q;
}

} // namespace

bool kiraLooksLikeTranslationTool(const String& original){
  String q=normalizeInput(original);
  String text,target;
  return parseTranslation(q,text,target);
}

bool kiraLooksLikeDictionaryTool(const String& original){
  String q=normalizeInput(original);
  return q.startsWith("define ") || q.startsWith("definition of ") ||
    q.startsWith("synonym of ") || q.startsWith("synonyms of ") ||
    q.startsWith("antonym of ") || q.startsWith("antonyms of ") ||
    (q.startsWith("what does ") && q.endsWith(" mean")) ||
    (q.startsWith("use ") && q.endsWith(" in a sentence"));
}

bool kiraHandleTranslationTool(String q){
  q=normalizeInput(q);
  String text,target;
  if(!parseTranslation(q,text,target)) return false;

  KiraV1::QueryFrame frame;
  makeGenericFrame(q,"translation","translate",frame);
  frame.entity=text;
  frame.criterion=target;

  String prompt=
    "Translate this text into "+target+": \""+text+"\". "
    "Give the translation directly. Preserve meaning and natural phrasing. "
    "Do not turn this into a web-search answer.";

  return runProviderRequest(frame,prompt);
}

bool kiraHandleDictionaryTool(String q){
  q=normalizeInput(q);
  if(!kiraLooksLikeDictionaryTool(q)) return false;

  // Definitions use the proven V1 structured router so ambiguity handling,
  // DictionaryAPI/Wikidata evidence, verification and follow-up context stay
  // exactly the same as before the Tool Engine existed.
  if(q.startsWith("define ") || q.startsWith("definition of ") ||
     (q.startsWith("what does ") && q.endsWith(" mean"))){
    if(kiraV1HandleStructuredWeb(q)) return true;
    // If the structured parser declines an unusual phrase, fall through to
    // the provider-backed language request instead of failing the tool.
  }

  String term=dictionaryTerm(q);
  KiraV1::QueryFrame frame;
  makeGenericFrame(q,term,"dictionary",frame);
  frame.entity=term;

  String prompt;
  if(q.startsWith("synonym")){
    prompt="Give 4 to 6 useful synonyms for \""+term+"\". Keep the answer concise and mention if meanings differ slightly.";
  }else if(q.startsWith("antonym")){
    prompt="Give 3 to 5 useful antonyms for \""+term+"\". Keep the answer concise.";
  }else if(q.startsWith("use ") && q.endsWith(" in a sentence")){
    String word=q.substring(4,q.length()-14); word.trim();
    frame.subject=word; frame.entity=word;
    prompt="Use the word \""+word+"\" correctly in one clear example sentence, then briefly state its meaning.";
  }else{
    prompt="Define \""+term+"\" in clear English. Give the main meaning and one short example sentence.";
  }

  return runProviderRequest(frame,prompt);
}
