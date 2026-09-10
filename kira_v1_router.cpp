#include <Arduino.h>
#include <WiFi.h>

#include "kira_v1_router.h"
#include "elli_intent.h"
#include "elli_query_frame.h"
#include "elli_entity_resolver.h"
#include "elli_context.h"
#include "kira_ai_web.h"
#include "kira_web_cache.h"
#include "kira_offline_brain.h"


// =====================================================
//         EXISTING FUNCTIONS IN kira_brain.ino
// =====================================================

void elliSay(const String& s);

bool tryOpenMeteo(
  const String& question,
  String& answer,
  String& source,
  int& confidence
);

void emitWebAnswer(
  const String& answer,
  const String& source,
  int confidence
);

bool httpsGet(
  const String& url,
  String& body,
  int& code
);

String urlEncode(
  const String& s
);

bool extractJsonStringAfter(
  const String& json,
  const String& key,
  int startAt,
  String& value,
  int& nextAt
);

String stripHtml(String input);


using namespace KiraV1;


static ConversationContext conversationContext;


// =====================================================
//                    SMALL HELPERS
// =====================================================

static String v1Normalize(String s){

  s.toLowerCase();

  String out="";
  bool space=false;


  for(size_t i=0;i<s.length();i++){

    char c=s[i];


    if(
      isalnum(
        (unsigned char)c
      )
    ){

      out+=c;
      space=false;
    }

    else if(
      out.length() &&
      !space
    ){

      out+=' ';
      space=true;
    }
  }


  out.trim();


  while(
    out.indexOf(
      "  "
    )>=0
  ){

    out.replace(
      "  ",
      " "
    );
  }


  return out;
}


static bool v1TitleContainsEntityPhrase(
  const String& title,
  const String& entity
){

  String a=
    " "+
    v1Normalize(title)+
    " ";


  String b=
    " "+
    v1Normalize(entity)+
    " ";


  if(
    b.length()<=2
  ){

    return false;
  }


  return
    a.indexOf(b)>=0;
}


static bool v1LocatableType(
  EntityType type
){

  return

    type==ENTITY_PLACE ||

    type==ENTITY_COUNTRY ||

    type==ENTITY_CITY ||

    type==ENTITY_MONUMENT ||

    type==ENTITY_HOTEL ||

    type==ENTITY_ORGANIZATION;
}


// =====================================================
//                     OFFLINE CACHE
// =====================================================

static void speakCached(
  const KiraCacheRecord& record
){

  String prefix=
    record.stale
    ? "My internet is unavailable, so this is older stored knowledge from "
    : "From my stored knowledge from ";


  prefix+=
    kiraCacheAgeText(
      record.storedEpoch
    )+
    ": ";


  emitWebAnswer(

    prefix+
    record.answer,

    "OFFLINE CACHE: "+
    record.source,

    max(
      30,
      record.confidence-
      (
        record.stale
        ? 15
        : 5
      )
    )
  );
}


// =====================================================
//         FALLBACK ENTITY AMBIGUITY PROBE
// =====================================================
//
// Important design change:
//
// The clarification system no longer depends ONLY on Groq
// voluntarily returning status=clarify.
//
// For entity-location questions KIRA performs one lightweight
// Wikipedia search first and inspects the strongest candidates.
//
// This means the top-two system can still work when:
//   - Groq key is empty
//   - Groq answers too confidently
//   - AI search is temporarily unavailable
//
// It does NOT use the Wikipedia result as final truth here.
// It only uses it to detect possible entity ambiguity.
//
// =====================================================

static int wikipediaAmbiguityCandidates(
  const QueryFrame& frame,
  Candidate output[],
  int maximum
){

  if(
    maximum<=0 ||
    WiFi.status()!=WL_CONNECTED
  ){

    return 0;
  }


  String entity=
    frame.entity.length()
    ? frame.entity
    : frame.subject;


  entity.trim();


  if(entity.length()<2){

    return 0;
  }


  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=query"

    "&list=search"

    "&srnamespace=0"

    "&srlimit=8"

    "&srprop=snippet"

    "&format=json"

    "&utf8=1"

    "&srsearch="+
    urlEncode(
      entity
    );


  String body;

  int code=0;


  if(
    !httpsGet(
      url,
      body,
      code
    )
  ){

    return 0;
  }


  int position=0;

  int count=0;


  while(
    count<maximum
  ){

    String title;
    String snippet;

    int afterTitle=0;
    int afterSnippet=0;


    if(
      !extractJsonStringAfter(
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
      !extractJsonStringAfter(
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
      stripHtml(
        snippet
      );


    Candidate candidate;

    candidate.title=
      title;

    candidate.description=
      snippet;

    candidate.source=
      "Wikipedia search";

    candidate.retrievalScore=
      max(
        55,
        96-
        count*7
      );


    candidate.type=
      inferEntityType(
        candidate
      );


    // Strongly reject pages that are obviously not normal entities.
    if(
      candidate.type==
      ENTITY_LIST_PAGE ||

      candidate.type==
      ENTITY_DISAMBIGUATION
    ){

      continue;
    }


    output[count++]=
      candidate;
  }


  return count;
}


// =====================================================
//        SHOULD KIRA ASK BETWEEN TOP TWO?
// =====================================================

static bool shouldAskTopTwoFromProbe(
  const QueryFrame& frame,
  Candidate candidates[],
  int count,
  int& firstIndex,
  int& secondIndex
){

  firstIndex=-1;
  secondIndex=-1;


  if(count<2){

    return false;
  }


  ResolutionDecision decision=
    resolveCandidates(
      frame,
      candidates,
      count
    );


  firstIndex=
    decision.firstIndex;

  secondIndex=
    decision.secondIndex;


  if(
    firstIndex<0 ||
    secondIndex<0
  ){

    return false;
  }


  // Standard confidence-gap ambiguity.
  if(
    decision.action==
    RESOLVE_ASK_TOP_TWO
  ){

    return true;
  }


  // ---------------------------------------------------
  // SAME-NAME ENTITY FAMILY
  // ---------------------------------------------------
  //
  // Example:
  //   Taj Mahal
  //   Taj Mahal Palace Hotel
  //
  // A generic semantic scorer may heavily favor the exact title.
  // But if both top candidates are real locatable entities and
  // both titles contain the complete requested entity phrase,
  // the user may genuinely mean either one.
  //
  // This is a CLASS rule, not a Taj-Mahal rule.
  // ---------------------------------------------------

  if(
    frame.intent==
    INTENT_ENTITY_LOCATION
  ){

    const Candidate& a=
      candidates[firstIndex];

    const Candidate& b=
      candidates[secondIndex];


    bool bothContainName=

      v1TitleContainsEntityPhrase(
        a.title,
        frame.entity
      )

      &&

      v1TitleContainsEntityPhrase(
        b.title,
        frame.entity
      );


    bool bothLocatable=

      v1LocatableType(
        a.type
      )

      &&

      v1LocatableType(
        b.type
      );


    if(
      bothContainName &&
      bothLocatable &&
      decision.secondScore>=65 &&
      decision.gap<=28
    ){

      return true;
    }
  }


  return false;
}


// =====================================================
//              AI AMBIGUITY POLICY
// =====================================================

static bool shouldAskTopTwoFromAi(
  const AiWebResult& result
){

  if(
    result.needsClarification
  ){

    return true;
  }


  if(
    result.candidateCount<2 ||

    !result.first.title.length() ||

    !result.second.title.length()
  ){

    return false;
  }


  int first=
    result.firstPlausibility;

  int second=
    result.secondPlausibility;


  // Ensure first >= second for the gap calculation.
  if(second>first){

    int temp=first;
    first=second;
    second=temp;
  }


  int gap=
    first-second;


  // User-requested behavior:
  // if two interpretations are both genuinely plausible and
  // close enough, ask rather than guessing.
  return

    first>=70 &&

    second>=58 &&

    gap<=25;
}


// =====================================================
//              STRUCTURED RANKING HELPERS
// =====================================================
//
// Ranking questions are not ordinary article lookups.
//
// Example class:
//
//   "most populated city in usa"
//
// means:
//
//   comparison set = city
//   region         = usa
//   metric         = population
//   operator       = MAX
//
// These helpers build a research request from the QueryFrame
// instead of sending a ranking problem to the old article-title
// similarity scorer.
// =====================================================

static String rankingDirection(
  QueryOperator op
){
  switch(op){

    case OP_MAX:
      return "greatest";

    case OP_MIN:
      return "smallest";

    case OP_BEST:
      return "best";

    case OP_WORST:
      return "worst";

    default:
      return "";
  }
}


static String buildRankingResearchQuestion(
  const QueryFrame& frame,
  const String& original
){
  String target=
    frame.subject;

  target.trim();


  // If subject extraction was weak, preserve the original
  // wording rather than inventing a target class.
  if(
    !target.length()
  ){
    String q=
      "Resolve this ranking question using reliable comparative evidence: ";

    q+=original;

    q+=
      ". Identify the comparison set, requested metric, region, and operator, "
      "then return the direct winning entity with the evidence relevant to that metric.";

    return q;
  }


  String q=
    "Identify the ";

  q+=target;


  String direction=
    rankingDirection(
      frame.op
    );


  if(direction.length()){

    q+=
      " with the ";

    q+=direction;
  }


  if(frame.metric.length()){

    q+=" ";

    q+=
      frame.metric;
  }


  if(frame.region.length()){

    q+=
      " in ";

    q+=
      frame.region;
  }


  if(frame.criterion.length()){

    q+=
      " according to ";

    q+=
      frame.criterion;
  }


  q+=
    ". This is a ranking problem, not an article-title matching problem. "
    "Compare the relevant candidates using reliable web evidence. "
    "Return the direct winning entity and the evidence for the requested metric. "
    "Do not reject the winner just because its page title does not contain the words of the question.";

  return q;
}


// =====================================================
//                    RUN QUERY FRAME
// =====================================================

static bool runFrame(
  QueryFrame frame,
  const String& original,
  const String& clarifiedEntity=""
){

  printQueryFrame(
    frame
  );


  // ===================================================
  // WEATHER
  // ===================================================

  if(
    frame.intent==
    INTENT_CURRENT_WEATHER
  ){

    if(!frame.location.length()){

      conversationContext.beginMissingLocation(
        frame
      );

      elliSay(
        conversationContext.prompt()
      );

      return true;
    }


    String answer;
    String source;

    int confidence=0;


    if(
      WiFi.status()==WL_CONNECTED &&

      tryOpenMeteo(
        "weather in "+
        frame.location,
        answer,
        source,
        confidence
      )
    ){

      emitWebAnswer(
        answer,
        source,
        confidence
      );


      kiraCacheStore(
        original,
        answer,
        source,
        confidence,
        kiraDefaultCacheTtl(
          original
        )
      );


      return true;
    }


    KiraCacheRecord cached;


    if(
      kiraCacheLookup(
        original,
        cached,
        true
      )
    ){

      speakCached(
        cached
      );

      return true;
    }


    elliSay(
      "I understood the weather request and the place, but I can't reach the live weather source right now."
    );


    return true;
  }


  // ===================================================
  // SUBJECTIVE RANKING
  // ===================================================
  //
  // A normal question such as "which is the best lip gloss" should
  // not be blocked just because the user did not name one metric.
  // In that case the AI may interpret BEST/WORST as an overall
  // comparison using the most relevant commonly accepted factors.
  // Explicit "by ..." wording still fills frame.criterion.
  // ===================================================

  if(
    frame.intent==
    INTENT_RANKING &&

    (
      frame.op==OP_BEST ||
      frame.op==OP_WORST
    ) &&

    !frame.criterion.length()
  ){

    Serial.println(
      "[RANKING] No explicit criterion -> using overall comparison."
    );

    frame.ambiguous=false;
  }


  // ===================================================
  // OFFLINE CACHE
  // ===================================================

  if(
    WiFi.status()!=WL_CONNECTED
  ){

    KiraCacheRecord cached;


    if(
      kiraCacheLookup(
        original,
        cached,
        true
      )
    ){

      speakCached(
        cached
      );

      return true;
    }


    // Ranking has a different evidence contract from ordinary
    // article lookup. Never push an unresolved ranking into the
    // old semantic title scorer.
    if(
      frame.intent==
      INTENT_RANKING
    ){

      elliSay(
        "I understand the ranking question, but the internet is unavailable and I don't have a stored verified answer for it yet."
      );

      return true;
    }


    // Other knowledge intents may still use the legacy offline
    // behavior when no cache entry exists.
    return false;
  }


  // ===================================================
  // ENTITY AMBIGUITY PROBE
  // ===================================================
  //
  // Skip it after the user has already clarified.
  // ===================================================

  if(
    frame.intent==
    INTENT_ENTITY_LOCATION &&

    !clarifiedEntity.length()
  ){

    Candidate probe[5];


    int count=
      wikipediaAmbiguityCandidates(
        frame,
        probe,
        5
      );


    int first=-1;
    int second=-1;


    if(
      shouldAskTopTwoFromProbe(
        frame,
        probe,
        count,
        first,
        second
      )
    ){

      conversationContext.beginEntityChoice(
        frame,
        probe[first],
        probe[second]
      );


      elliSay(
        conversationContext.prompt()
      );


      return true;
    }
  }


  // ===================================================
  // NO AI KEY
  // ===================================================
  //
  // We still performed ambiguity probing above.
  // Now fall through to the existing v0.9 web brain for
  // the final answer.
  // ===================================================

  if(
    !aiWebConfigured()
  ){

    if(
      frame.intent==
      INTENT_RANKING
    ){

      Serial.println(
        "[RANKING] AI web backend is not configured; legacy semantic ranking fallback is blocked."
      );

      elliSay(
        "I understand the ranking question, but the ranking research backend is not configured right now."
      );

      return true;
    }


    Serial.println(
      "[V1 AI] No AI provider configured -> using legacy web fallback."
    );

    return false;
  }


  // ===================================================
  // AI + WEB RESEARCH
  // ===================================================

 AiWebResult result;


bool aiSuccess=

  askGroqWeb(
    frame,
    original,
    result,
    clarifiedEntity
  );


// =====================================================
//              SPECIALIZED RANKING RETRY
// =====================================================
//
// If the normal AI response cannot be converted into a
// usable answer, rebuild the research request from:
//
//   comparison target
//   operator
//   metric
//   region
//   criterion
//
// This is generic ranking logic. No city/country/person
// name is hard-coded.
// =====================================================

if(
  !aiSuccess &&
  frame.intent==
  INTENT_RANKING
){

  Serial.println(
    "[RANKING] Primary AI result unusable -> structured ranking retry."
  );


  String retryQuestion=
    buildRankingResearchQuestion(
      frame,
      original
    );


  Serial.print(
    "[RANKING] Retry query: "
  );

  Serial.println(
    retryQuestion
  );


  AiWebResult retryResult;


  if(
    askGroqWeb(
      frame,
      retryQuestion,
      retryResult,
      clarifiedEntity
    )
  ){

    result=
      retryResult;

    aiSuccess=true;


    Serial.println(
      "[RANKING] Structured ranking retry succeeded."
    );
  }

  else{

    Serial.println(
      "[RANKING] Structured ranking retry failed."
    );
  }
}


// =====================================================
//                    FINAL FALLBACK
// =====================================================

if(!aiSuccess){

  KiraCacheRecord cached;


  if(
    kiraCacheLookup(
      original,
      cached,
      true
    )
  ){

    speakCached(
      cached
    );

    return true;
  }


  // ---------------------------------------------------
  // RANKING FAILURE IS CONSUMED HERE
  // ---------------------------------------------------
  //
  // The old Source Sense compares article/page similarity.
  // That is appropriate as a general fallback for some
  // knowledge lookups, but NOT for metric ranking.
  //
  // Example failure class:
  //
  //   correct candidate found
  //       ↓
  //   article title does not resemble whole question
  //       ↓
  //   candidate rejected
  //
  // Therefore ranking never falls through to that engine.
  // ---------------------------------------------------

  if(
    frame.intent==
    INTENT_RANKING
  ){

    elliSay(
      "I understood the ranking question, but I couldn't verify the ranking from my current online sources right now."
    );

    return true;
  }


  // Ordinary knowledge questions may still use the legacy
  // Source Sense as a final compatibility fallback.
  return false;
}

  // ===================================================
  // LOCAL TOP-TWO DECISION
  // ===================================================
  //
  // Do NOT rely only on the AI saying "clarify".
  // KIRA independently checks returned plausibility scores.
  // ===================================================

  if(
    !clarifiedEntity.length() &&

    shouldAskTopTwoFromAi(
      result
    )
  ){

    conversationContext.beginEntityChoice(
      frame,
      result.first,
      result.second
    );


    elliSay(
      conversationContext.prompt()
    );


    return true;
  }


  // If the backend itself marked the result as clarify but did
  // not provide a usable pair, do not invent an answer.
  if(
    result.needsClarification
  ){

    elliSay(
      "I found more than one possible meaning, but I couldn't form two reliable choices yet. Please describe which one you mean."
    );


    return true;
  }


  // ===================================================
  // FINAL AI ANSWER
  // ===================================================

  emitWebAnswer(
    result.answer,
    result.source,
    result.confidence
  );


  if(result.cacheable){
    kiraCacheStore(
      original,
      result.answer,
      result.source,
      result.confidence,
      kiraDefaultCacheTtl(
        original
      )
    );

    // V1.8 fallback cache: keep a small NVS copy of stable verified answers
    // so an ESP32-only exhibition build remains useful even without SD.
    kiraOfflineStoreVerified(
      original,
      result.answer,
      result.confidence
    );

    Serial.println(
      "[VERIFY V1] Verified answer accepted into persistent knowledge cache."
    );
  }
  else{
    Serial.println(
      "[VERIFY V1] Answer spoken but NOT cached because verification was insufficient or freshness-sensitive."
    );
  }


  return true;
}


// =====================================================
//                       PUBLIC API
// =====================================================

void kiraV1Begin(){

  kiraKnowledgeCacheBegin();


  Serial.println(
    "[KIRA v1] QueryFrame + context + offline cache ready."
  );


  Serial.println(

    aiWebConfigured()

    ? "[KIRA v1] AI web provider manager ENABLED (Gemini/Groq/OpenRouter failover)."

    : "[KIRA v1] No AI provider configured: ambiguity probe + legacy web fallback enabled."
  );
}


bool kiraV1HasPendingContext(){

  // live() silently clears stale context instead of consuming the
  // user's next real request.
  return
    conversationContext.live();
}


// =====================================================
//         SHOULD PENDING CONTEXT OWN THIS INPUT?
// =====================================================
//
// A clarification is interruptible.
//
// Short answers such as:
//   monument
//   India
//   QS
//   first
//   2
//
// belong to context.
//
// A complete new command/question such as:
//   what time is it
//   weather in Yavatmal
//   turn on the fan
//   set an alarm for 6 am
//
// cancels the old clarification and is routed normally.
// =====================================================

static int v1WordCount(const String& input){
  String q=v1Normalize(input);
  if(!q.length()) return 0;
  int count=1;
  for(size_t i=0;i<q.length();i++) if(q[i]==' ') count++;
  return count;
}


static bool v1LooksLikeExplicitQuestion(const String& input){
  String q=v1Normalize(input);
  const char* const starts[]={
    "what ","who ","where ","when ","why ","how ","which ",
    "is ","are ","was ","were ","do ","does ","did ",
    "can ","could ","would ","should ","will ",
    "weather ","temperature ","forecast ",
    "define ","explain ","tell ","give ","show ","compare ",
    "best ","top ","worst "
  };
  for(size_t i=0;i<sizeof(starts)/sizeof(starts[0]);i++){
    if(q.startsWith(starts[i])) return true;
  }
  return false;
}


bool kiraV1ShouldConsumePendingInput(
  const String& input
){
  if(
    !conversationContext.live()
  ){
    return false;
  }

  int intent=
    detectUtteranceIntent(
      input
    );

  // Commands always interrupt a pending knowledge clarification.
  bool definiteLocalRequest=
    intent==UTT_DEVICE ||
    intent==UTT_CLOCK ||
    intent==UTT_TASK ||
    intent==UTT_ROUTINE ||
    intent==UTT_PROFILE ||
    intent==UTT_SUPPORT;

  if(definiteLocalRequest){
    conversationContext.clear();

    Serial.println(
      "[CONTEXT] New standalone request -> previous clarification cleared."
    );

    return false;
  }

  // Bare short replies are often the ANSWER to Elli's pending question,
  // even when the generic intent detector labels them KNOWLEDGE/QUESTION.
  // Examples: "India", "QS", "thickness", "every", "overall".
  ContextKind pendingKind=conversationContext.kind();
  bool shortBareReply=
    v1WordCount(input)<=6 &&
    v1Normalize(input).length()<=64 &&
    !v1LooksLikeExplicitQuestion(input);

  if(
    shortBareReply &&
    (
      pendingKind==CONTEXT_MISSING_LOCATION ||
      pendingKind==CONTEXT_MISSING_REGION ||
      pendingKind==CONTEXT_MISSING_CRITERION ||
      pendingKind==CONTEXT_ENTITY_CHOICE
    )
  ){
    Serial.println(
      "[CONTEXT] Short reply -> pending clarification retained."
    );
    return true;
  }

  bool standaloneRequest=
    intent==UTT_QUESTION ||
    intent==UTT_CHAT;

  if(standaloneRequest){
    conversationContext.clear();

    Serial.println(
      "[CONTEXT] New standalone request -> previous clarification cleared."
    );

    return false;
  }

  return true;
}


void kiraV1ClearPendingContext(){
  conversationContext.clear();
}


bool kiraV1HandlePendingInput(
  const String& input
){

  if(
    !conversationContext.active()
  ){

    return false;
  }


  ContextResolution result=
    conversationContext.consume(
      input
    );


  if(
    result.result==
    CONTEXT_NEEDS_MORE ||

    result.result==
    CONTEXT_CANCELLED ||

    result.result==
    CONTEXT_EXPIRED
  ){

    elliSay(
      result.message
    );

    return true;
  }


  if(
    result.result!=
    CONTEXT_RESOLVED
  ){

    return false;
  }


  String clarified=

    result.hasChosenCandidate

    ? result.chosen.title

    : "";


  return
    runFrame(
      result.frame,
      result.frame.raw,
      clarified
    );
}


bool kiraV1HandleStructuredWeb(
  const String& input
){

  // ===================================================
  //              ACTION / COMMAND FIREWALL
  // ===================================================
  //
  // The AI/web brain is a KNOWLEDGE backend, not KIRA's actuator.
  // Local commands must fall through to the local router.
  //
  // This prevents:
  //   turn off light -> song search
  //   set alarm      -> "I cannot set alarms"
  //   start stopwatch-> stopwatch product/entity search
  // ===================================================

  int utterance=
    detectUtteranceIntent(
      input
    );

  if(
    utterance!=UTT_QUESTION
  ){
    return false;
  }


  QueryFrame frame;


  if(
    !analyzeQuery(
      input,
      frame
    )
  ){

    return false;
  }


  // ===================================================
  // INTENTS OWNED BY v1
  // ===================================================

  if(
    frame.intent==
    INTENT_CURRENT_WEATHER
  ){

    return
      runFrame(
        frame,
        input
      );
  }


  // Entity-location gets the local top-two ambiguity probe
  // even if Groq is not configured.
  if(
    frame.intent==
    INTENT_ENTITY_LOCATION
  ){

    return
      runFrame(
        frame,
        input
      );
  }


  // ===================================================
  // RANKING IS ALWAYS OWNED BY v1
  // ===================================================
  //
  // Ranking requires metric/comparison evidence. It must
  // never be delegated to the old article-similarity scorer.
  // runFrame() handles:
  //
  //   online AI research
  //   structured ranking retry
  //   offline cache
  //   missing-AI explanation
  // ===================================================

  if(
    frame.intent==
    INTENT_RANKING
  ){

    return
      runFrame(
        frame,
        input
      );
  }


  // Definitions and generic fact lookups use AI when it is
  // configured, or cache when offline. If neither applies,
  // legacy Source Sense remains available as compatibility
  // fallback for those NON-ranking question classes.
  if(
    frame.intent==
      INTENT_DEFINITION ||

    frame.intent==
      INTENT_FACT_LOOKUP
  ){

    if(
      aiWebConfigured() ||

      WiFi.status()!=WL_CONNECTED
    ){

      return
        runFrame(
          frame,
          input
        );
    }
  }


  return false;
}
