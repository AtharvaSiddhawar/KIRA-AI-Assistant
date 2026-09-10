#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "kira_ai_web.h"
#include "kira_wifi_max.h"
#include "kira_answer_verify.h"
#include "kira_secrets.h"

namespace KiraV1 {

// =====================================================
// KIRA WEB BRAIN V5.1b — FREE RETRIEVAL + FAILOVER
// =====================================================
//
// Current order:
//
// 1) FREE RETRIEVAL
//    - DuckDuckGo Instant Answer
//    - Wikipedia search snippets
//
// 2) Gemini reasoning
//    - gemini-3.5-flash-lite ONLY
//
//    3.7 and 3.6 were intentionally removed because this
//    project repeatedly hit transport failures before falling
//    back to 3.5 Flash-Lite. Free web evidence is still
//    collected first, then 3.5 Flash-Lite reasons over it.
//
// 3) Groq openai/gpt-oss-20b backup
//
// 4) OpenRouter openrouter/free backup
//
// Gemini/Groq/OpenRouter use provider health cooldowns so
// repeated rate limits or outages do not stall every request.
//
// No Tavily.
// No paid OpenRouter web plugin.
// No Groq Compound.
//
// Existing public function name askGroqWeb() is preserved
// for compatibility with kira_v1_router.cpp.
// =====================================================


#ifndef KIRA_GEMINI_API_KEY
#define KIRA_GEMINI_API_KEY ""
#endif

#ifndef KIRA_GROQ_API_KEY
#define KIRA_GROQ_API_KEY ""
#endif

#ifndef KIRA_OPENROUTER_API_KEY
#define KIRA_OPENROUTER_API_KEY ""
#endif


// =====================================================
// SMALL HELPERS
// =====================================================

static String jsonEscape(String s){

  String out="";

  for(size_t i=0;i<s.length();i++){

    char c=s[i];

    if(c=='\\' || c=='"'){
      out+='\\';
      out+=c;
    }
    else if(c=='\n'){
      out+="\\n";
    }
    else if(c=='\r'){
      // ignore
    }
    else if(c=='\t'){
      out+="\\t";
    }
    else{
      out+=c;
    }
  }

  return out;
}


static bool jsonStringAfter(
  const String& json,
  const String& key,
  int from,
  String& value,
  int& next
){

  String target=
    "\""+key+"\"";

  int keyPos=
    json.indexOf(target,from);

  if(keyPos<0){
    return false;
  }

  int colon=
    json.indexOf(
      ':',
      keyPos+target.length()
    );

  if(colon<0){
    return false;
  }

  int i=colon+1;

  while(
    i<json.length() &&
    isspace((unsigned char)json[i])
  ){
    i++;
  }

  // Strict JSON string parser.
  if(
    i>=json.length() ||
    json[i]!='"'
  ){
    return false;
  }

  String out="";
  bool escaped=false;

  for(i=i+1;i<json.length();i++){

    char c=json[i];

    if(escaped){

      if(c=='n') out+='\n';
      else if(c=='r') out+='\r';
      else if(c=='t') out+='\t';
      else out+=c;

      escaped=false;
      continue;
    }

    if(c=='\\'){
      escaped=true;
      continue;
    }

    if(c=='"'){

      value=out;
      next=i+1;

      return true;
    }

    out+=c;
  }

  return false;
}


static String urlEncodeLocal(const String& input){

  const char hex[]=
    "0123456789ABCDEF";

  String out="";

  for(size_t i=0;i<input.length();i++){

    unsigned char c=
      (unsigned char)input[i];

    if(
      (c>='a' && c<='z') ||
      (c>='A' && c<='Z') ||
      (c>='0' && c<='9') ||
      c=='-' ||
      c=='_' ||
      c=='.' ||
      c=='~'
    ){
      out+=(char)c;
    }
    else if(c==' '){
      out+="+";
    }
    else{
      out+='%';
      out+=hex[(c>>4)&0x0F];
      out+=hex[c&0x0F];
    }
  }

  return out;
}


static String stripHtmlLocal(String s){

  String out="";
  bool inTag=false;

  for(size_t i=0;i<s.length();i++){

    char c=s[i];

    if(c=='<'){
      inTag=true;
      continue;
    }

    if(c=='>'){
      inTag=false;
      continue;
    }

    if(!inTag){
      out+=c;
    }
  }

  out.replace("&quot;","\"");
  out.replace("&#039;","'");
  out.replace("&amp;","&");
  out.replace("&lt;","<");
  out.replace("&gt;",">");

  out.trim();

  return out;
}


static String cleanEnvelopeValue(String s){

  s.trim();

  if(
    s.startsWith("\"") &&
    s.endsWith("\"") &&
    s.length()>=2
  ){
    s=
      s.substring(
        1,
        s.length()-1
      );
  }

  s.trim();

  return s;
}


static String envelopeValue(
  const String& text,
  const String& key
){

  String token=
    key+"=";

  int pos=
    text.indexOf(token);

  if(pos<0){
    return "";
  }

  pos+=token.length();

  int end=
    text.indexOf(
      '\n',
      pos
    );

  if(end<0){
    end=text.length();
  }

  return
    cleanEnvelopeValue(
      text.substring(pos,end)
    );
}


static String envelopeAnswerValue(const String& text){
  String token="KIRA_ANSWER=";
  int pos=text.indexOf(token);
  if(pos<0) return "";
  pos+=token.length();
  int end=text.length();
  int nextField=text.indexOf("\nKIRA_",pos);
  if(nextField>=0) end=nextField;
  String value=text.substring(pos,end);
  value.trim();
  return cleanEnvelopeValue(value);
}


static int envelopeInt(
  const String& text,
  const String& key,
  int fallback
){

  String value=
    envelopeValue(text,key);

  if(!value.length()){
    return fallback;
  }

  for(size_t i=0;i<value.length();i++){

    if(i==0 && value[i]=='-'){
      continue;
    }

    if(!isdigit((unsigned char)value[i])){
      return fallback;
    }
  }

  return value.toInt();
}


static String stripCodeFences(String s){

  s.trim();

  if(s.startsWith("```")){

    int firstNewline=
      s.indexOf('\n');

    if(firstNewline>=0){
      s.remove(
        0,
        firstNewline+1
      );
    }

    int closing=
      s.lastIndexOf("```");

    if(closing>=0){
      s=
        s.substring(
          0,
          closing
        );
    }
  }

  s.trim();

  return s;
}


static bool looksLikeSchemaPlaceholder(String value){

  value.trim();
  value.toLowerCase();

  if(!value.length()){
    return true;
  }

  const char* const bad[]={
    "candidate1_title",
    "candidate1_type",
    "candidate1_description",
    "candidate1_score",
    "candidate2_title",
    "candidate2_type",
    "candidate2_description",
    "candidate2_score",
    "candidate_count",
    "null",
    "none",
    "n/a"
  };

  for(
    size_t i=0;
    i<sizeof(bad)/sizeof(bad[0]);
    i++
  ){
    if(value==bad[i]){
      return true;
    }
  }

  return
    value.startsWith("candidate1_") ||
    value.startsWith("candidate2_");
}


// =====================================================
// CONFIG
// =====================================================

static bool geminiConfigured(){
  return String(KIRA_GEMINI_API_KEY).length()>20;
}

static bool groqConfigured(){
  return String(KIRA_GROQ_API_KEY).length()>20;
}

static bool openRouterConfigured(){
  return String(KIRA_OPENROUTER_API_KEY).length()>20;
}


bool aiWebConfigured(){

  return
    geminiConfigured() ||
    groqConfigured() ||
    openRouterConfigured();
}


// =====================================================
// FREE PUBLIC WEB EVIDENCE
// =====================================================
//
// This is NOT a full Google replacement.
//
// It is a no-key evidence layer that improves reliability
// when Gemini Search grounding is unavailable.
//
// Sources:
//   - DuckDuckGo Instant Answer
//   - Wikipedia MediaWiki search
//
// No paid search API is used.
// =====================================================

struct FreeEvidence {
  bool success=false;
  bool duckDuckGo=false;
  bool wikipedia=false;
  uint8_t sourceCount=0;
  String text;
  String source;
};


static bool simpleGet(
  const String& url,
  String& body,
  int& code
){

  body="";
  code=0;

  KiraWifiRequestScope netScope(
    KIRA_NET_PRIORITY_WEB,
    "Public Web"
  );

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(kiraWifiMaxAdaptiveTimeout(12000));

  HTTPClient https;
  https.setTimeout(kiraWifiMaxAdaptiveTimeout(12000));
  https.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );

  if(!https.begin(client,url)){
    return false;
  }

  https.addHeader(
    "User-Agent",
    "KIRA-ESP32/1.0"
  );

  code=https.GET();

  netScope.setHttpCode(
    code
  );

  netScope.setSuccess(
    code>=200 &&
    code<300
  );

  if(code>0){
    body=https.getString();
  }

  https.end();

  return
    code>=200 &&
    code<300 &&
    body.length();
}


static void appendEvidence(
  String& target,
  const String& item,
  int maxTotal=2200
){

  if(!item.length()){
    return;
  }

  if(target.length()){
    target+="\n";
  }

  target+=item;

  if(target.length()>maxTotal){
    target=
      target.substring(
        0,
        maxTotal
      );
  }
}


static FreeEvidence fetchFreeEvidence(
  const String& query
){

  FreeEvidence out;

  if(WiFi.status()!=WL_CONNECTED){
    return out;
  }


  // ---------------------------------------------------
  // DuckDuckGo Instant Answer
  // ---------------------------------------------------

  {
    String body;
    int code=0;

    String url=
      "https://api.duckduckgo.com/"
      "?q="+
      urlEncodeLocal(query)+
      "&format=json"
      "&no_html=1"
      "&no_redirect=1"
      "&skip_disambig=0";


    if(simpleGet(url,body,code)){

      String answer;
      String abstractText;
      String abstractSource;

      int next=0;

      jsonStringAfter(
        body,
        "Answer",
        0,
        answer,
        next
      );

      next=0;

      jsonStringAfter(
        body,
        "AbstractText",
        0,
        abstractText,
        next
      );

      next=0;

      jsonStringAfter(
        body,
        "AbstractSource",
        0,
        abstractSource,
        next
      );


      if(answer.length()){

        out.duckDuckGo=true;

        appendEvidence(
          out.text,
          "DuckDuckGo answer: "+
          answer
        );
      }


      if(abstractText.length()){

        String abstractSourceNormalized=abstractSource;
        abstractSourceNormalized.toLowerCase();
        abstractSourceNormalized.trim();

        // DuckDuckGo often republishes Wikipedia as its abstract. Do not
        // count that as an independent second source family.
        if(abstractSourceNormalized != "wikipedia") out.duckDuckGo=true;

        String item=
          "DuckDuckGo abstract";

        if(abstractSource.length()){
          item+=" ("+abstractSource+")";
        }

        item+=": "+abstractText;

        appendEvidence(
          out.text,
          item
        );
      }
    }
  }


  // ---------------------------------------------------
  // Wikipedia search snippets
  // ---------------------------------------------------

  {
    String body;
    int code=0;

    String url=
      "https://en.wikipedia.org/w/api.php"
      "?action=query"
      "&list=search"
      "&srnamespace=0"
      "&srlimit=5"
      "&srprop=snippet"
      "&format=json"
      "&utf8=1"
      "&srsearch="+
      urlEncodeLocal(query);


    if(simpleGet(url,body,code)){

      int position=
        body.indexOf("\"search\"");

      if(position<0){
        position=0;
      }

      for(int i=0;i<5;i++){

        String title;
        String snippet;

        int afterTitle=0;
        int afterSnippet=0;


        if(
          !jsonStringAfter(
            body,
            "title",
            position,
            title,
            afterTitle
          )
        ){
          break;
        }


        if(
          !jsonStringAfter(
            body,
            "snippet",
            afterTitle,
            snippet,
            afterSnippet
          )
        ){
          break;
        }


        position=
          afterSnippet;


        snippet=
          stripHtmlLocal(
            snippet
          );


        if(snippet.length()>360){
          snippet=
            snippet.substring(
              0,
              360
            );
        }


        out.wikipedia=true;

        appendEvidence(
          out.text,
          "Wikipedia result: "+
          title+
          " — "+
          snippet
        );


        if(out.text.length()>=2100){
          break;
        }
      }
    }
  }


  out.sourceCount =
    (out.duckDuckGo ? 1 : 0) +
    (out.wikipedia ? 1 : 0);

  out.success=
    out.text.length()>20;

  if(out.success){
    out.source=
      "DuckDuckGo/Wikipedia free evidence";
  }

  return out;
}


// =====================================================
// QUESTION MODE / ADVICE SENSE
// =====================================================
//
// This is generic language-class logic.
//
// Advice, planning and feasibility questions should NOT be
// treated like encyclopedia-definition lookups.
//
// Examples:
//
//   what should i study after 10th
//   could i use an esp32 for this
//   which approach should i choose
//   is it possible to do this
// =====================================================

static bool providerAdviceOrFeasibility(
  String q
){

  q.toLowerCase();
  q.trim();


  const char* const phrases[]={

    " should i ",
    " could i ",
    " can i ",
    " would i ",
    " should we ",
    " could we ",
    " can we ",

    "what should ",
    "which should ",
    "how should ",

    "do i need to ",
    "do i have to ",

    "recommend ",
    "recommendation",
    "suggest ",
    "suggestion",
    "advise ",
    "advice",

    "is it possible ",
    "is this possible ",
    "would this work",
    "could this work",
    "can this work",

    "better option",
    "best approach",
    "pros and cons",
    "trade off",
    "tradeoff"
  };


  String padded=
    " "+
    q+
    " ";


  for(
    size_t i=0;
    i<
    sizeof(phrases)/
    sizeof(phrases[0]);
    i++
  ){

    String p=
      String(
        phrases[i]
      );


    if(
      p.startsWith(" ") &&
      p.endsWith(" ")
    ){

      if(
        padded.indexOf(p)>=0
      ){

        return true;
      }
    }

    else if(
      q.indexOf(p)>=0
    ){

      return true;
    }
  }


  return false;
}


// =====================================================
// PROMPT
// =====================================================

static String buildProviderPrompt(
  const QueryFrame& frame,
  const String& userQuestion,
  const String& clarifiedEntity,
  const FreeEvidence& evidence
){

  String prompt=

    "You are KIRA's knowledge, reasoning and advice engine. "
    "Answer the exact user request rather than forcing every question into an encyclopedia lookup. "

    "Use fluent conversational English. Avoid robotic filler and avoid repeating the user's entire sentence unless it improves clarity. "
    "For advice, planning or feasibility questions, give a direct useful answer, the main conditions or trade-offs, and ask one concise clarification only when missing context genuinely blocks a good answer. "

    "For ranking, compare the requested metric over the requested target set and region. "
    "For BEST/WORST questions with no explicit criterion, interpret the request as an overall comparison using the most relevant commonly accepted factors, state the main basis briefly, and do not ask for a criterion unless answering would otherwise be genuinely impossible. "
    "Do not use article-title similarity as ranking evidence. "

    "For named entities, create two candidates only when both are genuinely plausible meanings. "
    "Do not invent ambiguity. "

    "Never reinterpret device/alarm/timer/stopwatch/home-control commands as songs, films, "
    "books, tutorials, or unrelated entities. "

    "Use correct tense. Keep the spoken answer concise. "

    "If supplied WEB_EVIDENCE is relevant, use it. "
    "If it is insufficient for a time-sensitive claim, do not pretend the result is live. "

    "Return exactly this plain-text envelope, one field per line:\n"

    "KIRA_STATUS=ANSWER\n"
    "KIRA_CONFIDENCE=85\n"
    "KIRA_CANDIDATE_COUNT=0\n"
    "KIRA_CANDIDATE1_TITLE=\n"
    "KIRA_CANDIDATE1_TYPE=\n"
    "KIRA_CANDIDATE1_DESCRIPTION=\n"
    "KIRA_CANDIDATE1_SCORE=0\n"
    "KIRA_CANDIDATE2_TITLE=\n"
    "KIRA_CANDIDATE2_TYPE=\n"
    "KIRA_CANDIDATE2_DESCRIPTION=\n"
    "KIRA_CANDIDATE2_SCORE=0\n"
    "KIRA_ANSWER=answer here\n"

    "Use KIRA_STATUS=CLARIFY only for genuine unresolved two-way ambiguity. ";


  if(
    providerAdviceOrFeasibility(
      userQuestion
    )
  ){

    prompt+=
      "\nREQUEST_MODE=ADVICE_OR_FEASIBILITY"
      "\nTreat this as a decision/advice/capability question, not as a dictionary definition or simple fact lookup."
      "\nIf the user supplied conversation context for words such as this/that/it, use that context to resolve the reference.";
  }


  prompt+=
    "\nFRAME:"
    "\nintent="+String(intentName(frame.intent))+
    "\noperator="+String(operatorName(frame.op))+
    "\nsubject="+frame.subject+
    "\nentity="+frame.entity+
    "\nrelation="+frame.relation+
    "\nlocation="+frame.location+
    "\nregion="+frame.region+
    "\nmetric="+frame.metric+
    "\ncriterion="+frame.criterion;


  if(clarifiedEntity.length()){
    prompt+=
      "\nclarified_entity="+
      clarifiedEntity;
  }


  prompt+=
    "\nQUESTION="+
    userQuestion;


  if(evidence.success){

    prompt+=
      "\nWEB_EVIDENCE:\n"+
      evidence.text;
  }


  return prompt;
}


// =====================================================
// PARSE PROVIDER TEXT INTO KIRA RESULT
// =====================================================

static bool parseProviderText(
  String modelText,
  const String& providerLabel,
  const FreeEvidence& evidence,
  AiWebResult& result
){

  modelText=
    stripCodeFences(
      modelText
    );


  String status=
    envelopeValue(
      modelText,
      "KIRA_STATUS"
    );

  status.toLowerCase();


  result.confidence=
    constrain(
      envelopeInt(
        modelText,
        "KIRA_CONFIDENCE",
        evidence.success
        ?
        86
        :
        72
      ),
      0,
      100
    );


  result.candidateCount=
    constrain(
      envelopeInt(
        modelText,
        "KIRA_CANDIDATE_COUNT",
        0
      ),
      0,
      2
    );


  result.firstPlausibility=
    constrain(
      envelopeInt(
        modelText,
        "KIRA_CANDIDATE1_SCORE",
        0
      ),
      0,
      100
    );


  result.secondPlausibility=
    constrain(
      envelopeInt(
        modelText,
        "KIRA_CANDIDATE2_SCORE",
        0
      ),
      0,
      100
    );


  result.first.title=
    envelopeValue(
      modelText,
      "KIRA_CANDIDATE1_TITLE"
    );


  result.first.description=
    envelopeValue(
      modelText,
      "KIRA_CANDIDATE1_DESCRIPTION"
    );


  result.first.type=
    entityTypeFromText(
      envelopeValue(
        modelText,
        "KIRA_CANDIDATE1_TYPE"
      )
    );


  result.first.retrievalScore=
    result.firstPlausibility;


  result.second.title=
    envelopeValue(
      modelText,
      "KIRA_CANDIDATE2_TITLE"
    );


  result.second.description=
    envelopeValue(
      modelText,
      "KIRA_CANDIDATE2_DESCRIPTION"
    );


  result.second.type=
    entityTypeFromText(
      envelopeValue(
        modelText,
        "KIRA_CANDIDATE2_TYPE"
      )
    );


  result.second.retrievalScore=
    result.secondPlausibility;


  if(
    looksLikeSchemaPlaceholder(
      result.first.title
    )
  ){
    result.first.title="";
    result.first.description="";
    result.firstPlausibility=0;
  }


  if(
    looksLikeSchemaPlaceholder(
      result.second.title
    )
  ){
    result.second.title="";
    result.second.description="";
    result.secondPlausibility=0;
  }


  if(
    !result.first.title.length() ||
    !result.second.title.length()
  ){
    result.candidateCount=0;
  }


  result.answer=
    envelopeAnswerValue(
      modelText
    );


  // Provider ignored envelope but returned useful prose.
  if(!result.answer.length()){

    if(
      modelText.indexOf(
        "KIRA_STATUS="
      )<0
    ){
      result.answer=modelText;
    }
  }


  result.source=
    providerLabel;

  if(evidence.success){
    result.source+=
      " + free web evidence";
  }


  result.first.source=
    result.source;

  result.second.source=
    result.source;


  result.needsClarification=

    status=="clarify"

    &&

    result.candidateCount>=2

    &&

    result.first.title.length()

    &&

    result.second.title.length();


  result.success=

    result.needsClarification

    ||

    result.answer.length()>1;


  return result.success;
}


static void applyLocalVerification(
  const String& userQuestion,
  const FreeEvidence& evidence,
  AiWebResult& result
){
  if(!result.success || result.needsClarification) return;

  KiraVerificationReport report =
    kiraVerifyAnswer(
      userQuestion,
      result.answer,
      result.confidence,
      evidence.sourceCount
    );

  result.confidence = report.adjustedConfidence;
  result.verificationLevel = report.level;
  result.evidenceSources = report.evidenceSources;
  result.freshnessSensitive = report.freshnessSensitive;
  result.verified = report.verified;
  result.cacheable = report.cacheable;

  result.source += " [VERIFY:";
  result.source += kiraVerificationLevelName(report.level);
  result.source += "]";

  kiraRememberVerification(report);

  Serial.print("[VERIFY V1] ");
  Serial.print(kiraVerificationLevelName(report.level));
  Serial.print(" | evidence=");
  Serial.print(report.evidenceSources);
  Serial.print(" | confidence=");
  Serial.print(report.adjustedConfidence);
  Serial.print(" | cache=");
  Serial.println(report.cacheable ? "YES" : "NO");
}


// =====================================================
// STRUCTURED FREE-EVIDENCE QUERY
// =====================================================
//
// Search/retrieval wording should follow the QueryFrame instead
// of always sending the user's entire sentence.
//
// Example class:
//
//   "which is the most populated city in usa"
//
// becomes roughly:
//
//   "city population usa maximum ranking"
//
// No city/country/result is hard-coded.
// =====================================================

static String buildEvidenceQuery(
  const QueryFrame& frame,
  const String& original
){

  if(
    frame.intent==
    INTENT_RANKING
  ){

    String q="";


    if(frame.subject.length()){
      q+=frame.subject;
    }


    if(frame.metric.length()){

      if(q.length()){
        q+=" ";
      }

      q+=frame.metric;
    }


    if(frame.region.length()){

      if(q.length()){
        q+=" ";
      }

      q+=frame.region;
    }


    if(
      frame.op==
      OP_MAX
    ){

      q+=" maximum ranking";
    }

    else if(
      frame.op==
      OP_MIN
    ){

      q+=" minimum ranking";
    }

    else if(
      frame.op==
      OP_BEST
    ){

      q+=" best overall reviews comparison";
    }

    else if(
      frame.op==
      OP_WORST
    ){

      q+=" worst overall reviews comparison";
    }


    if(frame.criterion.length()){

      q+=" ";

      q+=frame.criterion;
    }


    q.trim();


    if(q.length()){
      return q;
    }
  }


  return original;
}


// =====================================================
// GEMINI
// =====================================================

static bool callGemini(
  const String& model,
  const String& prompt,
  String& modelText,
  int& httpCode,
  String& errorBody
){

  modelText="";
  errorBody="";
  httpCode=0;


  if(
    !geminiConfigured() ||
    WiFi.status()!=WL_CONNECTED
  ){

    return false;
  }


  // Gemini 3.x Free Tier:
  // inference is free, but API Google Search grounding is not.
  //
  // Therefore this request performs reasoning only over:
  //   - KIRA QueryFrame
  //   - free evidence gathered earlier
  //
  // Gemini 3.6/3.7 deprecated the old sampling parameters,
  // so temperature/top_p/top_k are deliberately omitted.

  String payload=

    "{\"contents\":[{"

      "\"role\":\"user\","

      "\"parts\":[{"
        "\"text\":\""+
        jsonEscape(
          prompt
        )+
        "\""
      "}]"

    "}],"

    "\"generationConfig\":{"

      "\"maxOutputTokens\":650,"

      "\"thinkingConfig\":{"
        "\"thinkingLevel\":\"low\""
      "}"

    "}"

    "}";


  String endpoint=

    "https://generativelanguage.googleapis.com/"
    "v1beta/models/"+
    model+
    ":generateContent";


  Serial.print(
    "[PROVIDER] Gemini "
  );

  Serial.print(
    model
  );

  Serial.println(
    " inference."
  );


  KiraWifiRequestScope netScope(
    KIRA_NET_PRIORITY_AI,
    "Gemini AI"
  );


  WiFiClientSecure client;

  client.setInsecure();

  client.setTimeout(
    kiraWifiMaxAdaptiveTimeout(
      22000
    )
  );


  HTTPClient https;

  https.setTimeout(
    kiraWifiMaxAdaptiveTimeout(
      22000
    )
  );

  https.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );


  if(
    !https.begin(
      client,
      endpoint
    )
  ){

    return false;
  }


  https.addHeader(
    "x-goog-api-key",
    String(
      KIRA_GEMINI_API_KEY
    )
  );


  https.addHeader(
    "Content-Type",
    "application/json"
  );


  https.addHeader(
    "User-Agent",
    "KIRA-ESP32/1.0"
  );


  httpCode=
    https.POST(
      payload
    );


  netScope.setHttpCode(
    httpCode
  );

  netScope.setSuccess(
    httpCode>=200 &&
    httpCode<300
  );


  String body=

    httpCode>0

    ?

    https.getString()

    :

    "";


  https.end();


  Serial.print(
    "[PROVIDER] Gemini HTTP "
  );

  Serial.println(
    httpCode
  );


  if(
    httpCode<200 ||
    httpCode>=300 ||
    !body.length()
  ){

    errorBody=
      body;


    if(
      errorBody.length()>500
    ){

      errorBody=
        errorBody.substring(
          0,
          500
        );
    }


    return false;
  }


  int candidatesAt=
    body.indexOf(
      "\"candidates\""
    );


  int next=0;


  return
    jsonStringAfter(
      body,
      "text",
      candidatesAt>=0
      ?
      candidatesAt
      :
      0,
      modelText,
      next
    );
}


// =====================================================
// GEMINI MODEL FAILOVER / CAPABILITY MEMORY
// =====================================================
//
// Stable production model:
//
//   3.5 Flash-Lite ONLY
//
// KIRA first gathers free web evidence, then sends the prompt
// to gemini-3.5-flash-lite. 3.7 and 3.6 are intentionally not
// present in this build, so there is no startup/request delay
// from trying those failing models first.
//
// If 3.5 Flash-Lite itself is temporarily unavailable, the
// existing circuit-breaker logic still skips it appropriately
// and KIRA can continue to the configured backup providers.
// =====================================================

static int geminiPreferredModel=-1;

static uint8_t geminiUnavailableMask=0;


// =====================================================
// PROVIDER HEALTH / CIRCUIT BREAKER
// =====================================================
//
// Temporary provider failures should not punish every
// following exhibition question with the same long timeout.
//
// 404  -> model unavailable for the whole boot
// 503  -> model cooldown for 2 minutes
// < 0  -> transport/model cooldown for 30 seconds
// 429  -> Gemini provider cooldown for 5 minutes
// 401/403 -> Gemini provider cooldown for 10 minutes
// =====================================================

static uint32_t geminiProviderCooldownUntil=0;


static const char* const KIRA_GEMINI_MODELS[]={

  "gemini-3.5-flash-lite"
};


static const int KIRA_GEMINI_MODEL_COUNT=

  sizeof(KIRA_GEMINI_MODELS)/
  sizeof(KIRA_GEMINI_MODELS[0]);


static uint32_t geminiModelCooldownUntil[
  KIRA_GEMINI_MODEL_COUNT
]={0};


static bool providerCooldownActive(
  uint32_t untilMs
){

  if(!untilMs){
    return false;
  }


  return
    (int32_t)(
      untilMs-
      millis()
    )>0;
}


// =====================================================
// BACKUP PROVIDER HEALTH
// =====================================================
//
// Gemini already has per-model/provider circuit breakers.
// Keep the same protection for Groq and OpenRouter so a
// dead/rate-limited backup does not cost another 20+ seconds
// on every following question.
// =====================================================

static uint32_t groqProviderCooldownUntil=0;
static uint32_t openRouterProviderCooldownUntil=0;


static uint32_t providerCooldownRemainingMs(uint32_t untilMs){
  if(!providerCooldownActive(untilMs)) return 0;
  return untilMs - millis();
}

bool aiProvidersUsable(){
  bool geminiReady=false;
  if(geminiConfigured() && !providerCooldownActive(geminiProviderCooldownUntil)){
    for(int i=0;i<KIRA_GEMINI_MODEL_COUNT;i++){
      bool unavailable=(geminiUnavailableMask & (1U<<i))!=0;
      if(!unavailable && !providerCooldownActive(geminiModelCooldownUntil[i])){
        geminiReady=true;
        break;
      }
    }
  }

  bool groqReady=groqConfigured() && !providerCooldownActive(groqProviderCooldownUntil);
  bool openRouterReady=openRouterConfigured() && !providerCooldownActive(openRouterProviderCooldownUntil);
  return geminiReady || groqReady || openRouterReady;
}

String aiProviderStatusCompact(){
  String out = "AI providers: Gemini ";
  out += geminiConfigured() ? "configured" : "not configured";
  if(providerCooldownActive(geminiProviderCooldownUntil)) out += " (cooldown)";
  out += ", Groq ";
  out += groqConfigured() ? "configured" : "not configured";
  if(providerCooldownActive(groqProviderCooldownUntil)) out += " (cooldown)";
  out += ", OpenRouter ";
  out += openRouterConfigured() ? "configured" : "not configured";
  if(providerCooldownActive(openRouterProviderCooldownUntil)) out += " (cooldown)";
  out += ".";
  return out;
}

void printAiProviderStatus(){
  Serial.println();
  Serial.println("========== AI PROVIDER STATUS ==========");

  Serial.print("Gemini configured : "); Serial.println(geminiConfigured() ? "YES" : "NO");
  Serial.print("Gemini cooldown   : ");
  if(providerCooldownActive(geminiProviderCooldownUntil)){
    Serial.print(providerCooldownRemainingMs(geminiProviderCooldownUntil) / 1000UL);
    Serial.println(" s remaining");
  }else Serial.println("READY");

  Serial.print("Preferred model   : ");
  if(geminiPreferredModel >= 0 && geminiPreferredModel < KIRA_GEMINI_MODEL_COUNT)
    Serial.println(KIRA_GEMINI_MODELS[geminiPreferredModel]);
  else
    Serial.println("AUTO");

  for(int i = 0; i < KIRA_GEMINI_MODEL_COUNT; i++){
    Serial.print("  "); Serial.print(KIRA_GEMINI_MODELS[i]); Serial.print(" : ");
    if(geminiUnavailableMask & (1U << i)) Serial.println("UNAVAILABLE THIS BOOT");
    else if(providerCooldownActive(geminiModelCooldownUntil[i])){
      Serial.print("COOLDOWN ");
      Serial.print(providerCooldownRemainingMs(geminiModelCooldownUntil[i]) / 1000UL);
      Serial.println(" s");
    }else Serial.println("READY");
  }

  Serial.print("Groq configured   : "); Serial.println(groqConfigured() ? "YES" : "NO");
  Serial.print("Groq cooldown     : ");
  if(providerCooldownActive(groqProviderCooldownUntil)){
    Serial.print(providerCooldownRemainingMs(groqProviderCooldownUntil) / 1000UL); Serial.println(" s remaining");
  }else Serial.println("READY");

  Serial.print("OpenRouter config : "); Serial.println(openRouterConfigured() ? "YES" : "NO");
  Serial.print("OpenRouter cooldown: ");
  if(providerCooldownActive(openRouterProviderCooldownUntil)){
    Serial.print(providerCooldownRemainingMs(openRouterProviderCooldownUntil) / 1000UL); Serial.println(" s remaining");
  }else Serial.println("READY");

  Serial.println("========================================");
}


static void updateBackupProviderHealth(
  const char* label,
  int httpCode,
  uint32_t& cooldownUntil
){

  uint32_t duration=0;
  const char* reason=nullptr;


  if(httpCode==429){
    duration=5UL*60UL*1000UL;
    reason="rate limited";
  }
  else if(httpCode==401 || httpCode==403){
    duration=10UL*60UL*1000UL;
    reason="authentication/access failure";
  }
  else if(httpCode<0){
    duration=30000UL;
    reason="transport failure";
  }
  else if(httpCode>=500 || httpCode==408 || httpCode==425){
    duration=120000UL;
    reason="temporary provider failure";
  }


  if(!duration){
    return;
  }


  cooldownUntil=
    millis()+
    duration;


  Serial.print("[PROVIDER] " );
  Serial.print(label);
  Serial.print(" " );
  Serial.print(reason);
  Serial.println(" -> cooldown enabled.");
}


static void setGeminiModelCooldown(
  int index,
  uint32_t durationMs
){

  if(
    index<0 ||
    index>=KIRA_GEMINI_MODEL_COUNT
  ){
    return;
  }


  geminiModelCooldownUntil[index]=
    millis()+
    durationMs;


  if(
    geminiPreferredModel==
    index
  ){

    geminiPreferredModel=-1;
  }
}


static bool tryGeminiModelIndex(
  int index,
  const String& prompt,
  String& modelText,
  String& providerLabel,
  int& httpCode
){

  if(
    index<0 ||
    index>=KIRA_GEMINI_MODEL_COUNT
  ){

    return false;
  }


  if(
    geminiUnavailableMask &
    (1U<<index)
  ){

    return false;
  }


  if(
    providerCooldownActive(
      geminiModelCooldownUntil[index]
    )
  ){

    Serial.print(
      "[PROVIDER] Gemini model cooling down -> skip: "
    );

    Serial.println(
      KIRA_GEMINI_MODELS[index]
    );


    httpCode=-1000;

    return false;
  }


  String errorBody;


  bool ok=
    callGemini(
      KIRA_GEMINI_MODELS[index],
      prompt,
      modelText,
      httpCode,
      errorBody
    );


  if(ok){

    geminiModelCooldownUntil[index]=0;

    geminiPreferredModel=
      index;


    providerLabel=

      "Gemini "+

      String(
        KIRA_GEMINI_MODELS[index]
      );


    return true;
  }


  // A 404 means this exact model isn't available to this
  // API project. Remember that capability failure.
  if(
    httpCode==404
  ){

    geminiUnavailableMask|=

      (uint8_t)(
        1U<<index
      );


    if(
      geminiPreferredModel==
      index
    ){

      geminiPreferredModel=-1;
    }


    Serial.print(
      "[PROVIDER] Gemini model unavailable -> disabled for this boot: "
    );

    Serial.println(
      KIRA_GEMINI_MODELS[index]
    );
  }


  else if(
    httpCode==503
  ){

    setGeminiModelCooldown(
      index,
      120000UL
    );


    Serial.print(
      "[PROVIDER] Gemini model busy -> 2 minute cooldown: "
    );

    Serial.println(
      KIRA_GEMINI_MODELS[index]
    );
  }


  else if(
    httpCode<0
  ){

    setGeminiModelCooldown(
      index,
      30000UL
    );


    Serial.print(
      "[PROVIDER] Gemini transport failure -> 30 second cooldown: "
    );

    Serial.println(
      KIRA_GEMINI_MODELS[index]
    );
  }


  else if(
    httpCode==429
  ){

    geminiProviderCooldownUntil=
      millis()+
      5UL*60UL*1000UL;


    Serial.println(
      "[PROVIDER] Gemini rate-limited -> 5 minute provider cooldown."
    );
  }


  else if(
    httpCode==401 ||
    httpCode==403
  ){

    geminiProviderCooldownUntil=
      millis()+
      10UL*60UL*1000UL;


    Serial.println(
      "[PROVIDER] Gemini authentication/access issue -> 10 minute provider cooldown."
    );
  }


  else if(
    errorBody.length()
  ){

    Serial.print(
      "[PROVIDER] Gemini diagnostic: "
    );

    Serial.println(
      errorBody
    );
  }


  return false;
}



static bool callBestGemini(
  const String& prompt,
  String& modelText,
  String& providerLabel
){

  if(
    !geminiConfigured()
  ){

    return false;
  }


  if(
    providerCooldownActive(
      geminiProviderCooldownUntil
    )
  ){

    Serial.println(
      "[PROVIDER] Gemini provider cooling down -> switching provider."
    );

    return false;
  }


  // First use the model that already succeeded earlier in
  // this boot. This removes repeated model-discovery latency.
  if(
    geminiPreferredModel>=0
  ){

    int code=0;


    if(
      tryGeminiModelIndex(
        geminiPreferredModel,
        prompt,
        modelText,
        providerLabel,
        code
      )
    ){

      return true;
    }


    // 429 / auth failures are provider-level signals.
    // Don't waste time walking every model.
    if(
      code==429 ||
      code==401 ||
      code==403
    ){

      return false;
    }
  }


  for(
    int i=0;
    i<KIRA_GEMINI_MODEL_COUNT;
    i++
  ){

    if(
      i==
      geminiPreferredModel
    ){

      continue;
    }


    int code=0;


    if(
      tryGeminiModelIndex(
        i,
        prompt,
        modelText,
        providerLabel,
        code
      )
    ){

      return true;
    }


    if(
      code==429 ||
      code==401 ||
      code==403
    ){

      return false;
    }
  }


  return false;
}


// =====================================================
// GROQ NORMAL INFERENCE
// =====================================================

static bool callGroq(
  const String& prompt,
  String& modelText
){

  modelText="";


  if(
    !groqConfigured() ||
    WiFi.status()!=WL_CONNECTED
  ){
    return false;
  }


  if(
    providerCooldownActive(
      groqProviderCooldownUntil
    )
  ){

    Serial.println(
      "[PROVIDER] Groq cooling down -> switching provider."
    );

    return false;
  }


  String payload=

    "{\"model\":\"openai/gpt-oss-20b\","
    "\"messages\":[{"
      "\"role\":\"user\","
      "\"content\":\""+
      jsonEscape(prompt)+
      "\""
    "}],"
    "\"temperature\":0.1,"
    "\"max_completion_tokens\":650"
    "}";


  Serial.println(
    "[PROVIDER] Groq GPT-OSS 20B."
  );


  KiraWifiRequestScope netScope(
    KIRA_NET_PRIORITY_AI,
    "Groq AI"
  );

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(
    kiraWifiMaxAdaptiveTimeout(
      20000
    )
  );

  HTTPClient https;
  https.setTimeout(
    kiraWifiMaxAdaptiveTimeout(
      20000
    )
  );
  https.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );


  if(
    !https.begin(
      client,
      "https://api.groq.com/openai/v1/chat/completions"
    )
  ){
    return false;
  }


  https.addHeader(
    "Authorization",
    "Bearer "+String(KIRA_GROQ_API_KEY)
  );

  https.addHeader(
    "Content-Type",
    "application/json"
  );


  int code=
    https.POST(payload);


  netScope.setHttpCode(
    code
  );

  netScope.setSuccess(
    code>=200 &&
    code<300
  );


  String body=
    code>0
    ?
    https.getString()
    :
    "";


  https.end();


  Serial.print(
    "[PROVIDER] Groq HTTP "
  );

  Serial.println(code);


  if(
    code<200 ||
    code>=300 ||
    !body.length()
  ){

    updateBackupProviderHealth(
      "Groq",
      code,
      groqProviderCooldownUntil
    );

    return false;
  }


  // A successful transport proves the provider is healthy again.
  groqProviderCooldownUntil=0;


  int messageAt=
    body.indexOf(
      "\"message\""
    );

  int next=0;


  return
    jsonStringAfter(
      body,
      "content",
      messageAt>=0
      ?
      messageAt
      :
      0,
      modelText,
      next
    );
}


// =====================================================
// OPENROUTER FREE INFERENCE
// =====================================================

static bool callOpenRouter(
  const String& prompt,
  String& modelText
){

  modelText="";


  if(
    !openRouterConfigured() ||
    WiFi.status()!=WL_CONNECTED
  ){
    return false;
  }


  if(
    providerCooldownActive(
      openRouterProviderCooldownUntil
    )
  ){

    Serial.println(
      "[PROVIDER] OpenRouter cooling down -> no retry this turn."
    );

    return false;
  }


  String payload=

    "{\"model\":\"openrouter/free\","
    "\"messages\":[{"
      "\"role\":\"user\","
      "\"content\":\""+
      jsonEscape(prompt)+
      "\""
    "}],"
    "\"temperature\":0.1,"
    "\"max_tokens\":650"
    "}";


  Serial.println(
    "[PROVIDER] OpenRouter FREE."
  );


  KiraWifiRequestScope netScope(
    KIRA_NET_PRIORITY_AI,
    "OpenRouter AI"
  );

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(
    kiraWifiMaxAdaptiveTimeout(
      22000
    )
  );

  HTTPClient https;
  https.setTimeout(
    kiraWifiMaxAdaptiveTimeout(
      22000
    )
  );
  https.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );


  if(
    !https.begin(
      client,
      "https://openrouter.ai/api/v1/chat/completions"
    )
  ){
    return false;
  }


  https.addHeader(
    "Authorization",
    "Bearer "+String(KIRA_OPENROUTER_API_KEY)
  );

  https.addHeader(
    "Content-Type",
    "application/json"
  );

  https.addHeader(
    "X-Title",
    "KIRA ESP32 Assistant"
  );


  int code=
    https.POST(payload);


  netScope.setHttpCode(
    code
  );

  netScope.setSuccess(
    code>=200 &&
    code<300
  );


  String body=
    code>0
    ?
    https.getString()
    :
    "";


  https.end();


  Serial.print(
    "[PROVIDER] OpenRouter HTTP "
  );

  Serial.println(code);


  if(
    code<200 ||
    code>=300 ||
    !body.length()
  ){

    updateBackupProviderHealth(
      "OpenRouter",
      code,
      openRouterProviderCooldownUntil
    );

    return false;
  }


  openRouterProviderCooldownUntil=0;


  int messageAt=
    body.indexOf(
      "\"message\""
    );

  int next=0;


  return
    jsonStringAfter(
      body,
      "content",
      messageAt>=0
      ?
      messageAt
      :
      0,
      modelText,
      next
    );
}


// =====================================================
// PUBLIC COMPATIBILITY FUNCTION
// =====================================================

bool askGroqWeb(
  const QueryFrame& frame,
  const String& userQuestion,
  AiWebResult& result,
  const String& clarifiedEntity,
  bool skipFreeEvidence,
  const String& evidenceQueryOverride
){

  result=AiWebResult();


  if(
    WiFi.status()!=WL_CONNECTED
  ){

    return false;
  }


  Serial.println(
    "[WEB V5.1b] Exhibition provider manager + circuit breaker."
  );


  // ===================================================
  // 1) FREE RETRIEVAL FIRST
  // ===================================================
  //
  // Gemini 3.x Search grounding is not part of the API
  // Free Tier, so V5.1 never attempts it.
  //
  // Retrieval is instead obtained from free public APIs:
  //
  //   DuckDuckGo Instant Answer
  //   Wikipedia MediaWiki search
  //
  // Then the AI provider reasons over that evidence.
  // ===================================================

  FreeEvidence evidence;

  if(skipFreeEvidence){
    // Tool requests such as translation do not benefit from encyclopedia
    // retrieval. Skip DDG/Wikipedia to reduce latency and avoid irrelevant
    // evidence; local verification will correctly classify the result AI_ONLY.
    Serial.println("[WEB V5.1b] Direct reasoning mode: free evidence skipped by tool policy.");
  }else{
    String evidenceQuery=
      evidenceQueryOverride.length()
      ? evidenceQueryOverride
      : buildEvidenceQuery(
          frame,
          userQuestion
        );

    Serial.print("[WEB V5.1b] Evidence query: ");
    Serial.println(evidenceQuery);

    evidence=fetchFreeEvidence(evidenceQuery);

    Serial.print("[WEB V5.1b] Free evidence: ");
    Serial.println(evidence.success ? "YES" : "NO");
    Serial.print("[WEB V5.1b] Independent evidence families: ");
    Serial.println(evidence.sourceCount);
  }


  String prompt=

    buildProviderPrompt(
      frame,
      userQuestion,
      clarifiedEntity,
      evidence
    );


  // ===================================================
  // 2) GEMINI 3.x — PRIMARY FREE REASONING
  // ===================================================

  if(
    geminiConfigured()
  ){

    String modelText;
    String providerLabel;


    if(
      callBestGemini(
        prompt,
        modelText,
        providerLabel
      )
    ){

      if(
        parseProviderText(
          modelText,
          providerLabel,
          evidence,
          result
        )
      ){

        applyLocalVerification(userQuestion,evidence,result);
        return true;
      }
    }
  }


  // ===================================================
  // 3) GROQ — BACKUP #1
  // ===================================================

  if(
    groqConfigured()
  ){

    String modelText;


    if(
      callGroq(
        prompt,
        modelText
      )
    ){

      if(
        parseProviderText(
          modelText,
          "Groq GPT-OSS 20B",
          evidence,
          result
        )
      ){

        applyLocalVerification(userQuestion,evidence,result);
        return true;
      }
    }
  }


  // ===================================================
  // 4) OPENROUTER FREE — BACKUP #2
  // ===================================================

  if(
    openRouterConfigured()
  ){

    String modelText;


    if(
      callOpenRouter(
        prompt,
        modelText
      )
    ){

      if(
        parseProviderText(
          modelText,
          "OpenRouter FREE",
          evidence,
          result
        )
      ){

        applyLocalVerification(userQuestion,evidence,result);
        return true;
      }
    }
  }


  // Existing kira_v1_router.cpp will now try KIRA's
  // persistent verified cache / ranking-safe fallback.
  return false;
}

bool aiWebEnvelopeParserSelfTest(){
  FreeEvidence evidence;
  AiWebResult result;
  String sample=
    "KIRA_STATUS=ANSWER\n"
    "KIRA_CONFIDENCE=88\n"
    "KIRA_CANDIDATE_COUNT=0\n"
    "KIRA_CANDIDATE1_TITLE=\n"
    "KIRA_CANDIDATE1_TYPE=\n"
    "KIRA_CANDIDATE1_DESCRIPTION=\n"
    "KIRA_CANDIDATE1_SCORE=0\n"
    "KIRA_CANDIDATE2_TITLE=\n"
    "KIRA_CANDIDATE2_TYPE=\n"
    "KIRA_CANDIDATE2_DESCRIPTION=\n"
    "KIRA_CANDIDATE2_SCORE=0\n"
    "KIRA_ANSWER=Here are useful synonyms:\n"
    "- smart\n"
    "- clever\n"
    "- bright";
  bool ok=parseProviderText(sample,"SELFTEST",evidence,result);
  return ok && result.answer.indexOf("smart")>=0 && result.answer.indexOf("clever")>=0 && result.answer.indexOf("bright")>=0;
}


} // namespace KiraV1
