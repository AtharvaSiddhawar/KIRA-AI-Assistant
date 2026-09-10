#include <Arduino.h>

#include "elli_arbiter.h"
#include "elli_intent.h"
#include "elli_nlu.h"
#include "elli_semantic_v2.h"


// =====================================================
// INTERNAL SCORE HELPERS
// =====================================================

static void arbAdd(
  ElliArbiterDecision& d,
  ElliArbiterClass cls,
  int amount
){

  if(
    cls<=ELLI_ARB_NONE ||
    cls>=ELLI_ARB_CLASS_COUNT ||
    amount<=0
  ){

    return;
  }


  int value=
    (int)d.score[cls]+
    amount;


  d.score[cls]=
    (uint8_t)constrain(
      value,
      0,
      100
    );
}


static void arbFloor(
  ElliArbiterDecision& d,
  ElliArbiterClass cls,
  int minimum
){

  if(
    cls<=ELLI_ARB_NONE ||
    cls>=ELLI_ARB_CLASS_COUNT
  ){

    return;
  }


  if(
    d.score[cls]<
    minimum
  ){

    d.score[cls]=
      (uint8_t)constrain(
        minimum,
        0,
        100
      );
  }
}


static String arbNormalize(String q){

  q.trim();
  q.toLowerCase();

  q.replace("?","");
  q.replace("!","");
  q.replace(".","");
  q.replace(",","");
  q.replace(":","");
  q.replace(";","");

  while(
    q.indexOf("  ")>=0
  ){

    q.replace("  "," ");
  }

  q.trim();

  return q;
}


static bool arbLocalStop(String q){

  q=
    arbNormalize(
      q
    );


  return
    q=="stop" ||
    q=="stop now" ||
    q=="please stop";
}


static bool arbStrongLocalClass(
  ElliArbiterClass cls
){

  return
    cls==ELLI_ARB_CLOCK ||
    cls==ELLI_ARB_DEVICE ||
    cls==ELLI_ARB_TASK ||
    cls==ELLI_ARB_ROUTINE;
}


// =====================================================
// SIGNAL 1: CURRENT PROVEN INTENT DETECTOR
// =====================================================
//
// This is a PRIOR, not the final decision.
//
// The point of V1 is to fuse this with NLU + semantic meaning,
// then observe disagreements before allowing takeover.
// =====================================================

static void arbScoreLegacyIntent(
  ElliArbiterDecision& d,
  int intent,
  const ElliNLUFrame& nlu
){

  switch(intent){

    case UTT_CLOCK:

      arbAdd(
        d,
        ELLI_ARB_CLOCK,
        90
      );

      break;


    case UTT_DEVICE:

      arbAdd(
        d,
        ELLI_ARB_DEVICE,
        90
      );

      break;


    case UTT_TASK:

      arbAdd(
        d,
        ELLI_ARB_TASK,
        90
      );

      break;


    case UTT_ROUTINE:

      arbAdd(
        d,
        ELLI_ARB_ROUTINE,
        88
      );

      break;


    case UTT_PROFILE:

      arbAdd(
        d,
        ELLI_ARB_PERSONAL_MEMORY,
        86
      );

      break;


    case UTT_SUPPORT:

      arbAdd(
        d,
        ELLI_ARB_SUPPORT,
        90
      );

      break;


    case UTT_QUESTION:

      arbAdd(
        d,
        ELLI_ARB_KNOWLEDGE,
        76
      );

      break;


    case UTT_CHAT:

      arbAdd(
        d,
        ELLI_ARB_CHAT,
        86
      );

      break;


    case UTT_STATEMENT:

      if(nlu.personal){

        arbAdd(
          d,
          ELLI_ARB_PERSONAL_CONVERSATION,
          60
        );
      }

      else{

        arbAdd(
          d,
          ELLI_ARB_STATEMENT,
          62
        );
      }

      break;
  }
}


// =====================================================
// SIGNAL 2: UNIVERSAL NLU
// =====================================================

static void arbScoreNLU(
  ElliArbiterDecision& d,
  const ElliNLUFrame& nlu
){

  if(nlu.question){

    arbAdd(
      d,
      ELLI_ARB_KNOWLEDGE,
      16
    );


    if(nlu.personal){

      arbAdd(
        d,
        ELLI_ARB_PERSONAL_MEMORY,
        8
      );
    }
  }


  if(nlu.command){

    // A command should prefer action routes over knowledge,
    // but object/domain ownership still comes from the proven
    // intent detector and local handlers.
    arbAdd(
      d,
      ELLI_ARB_DEVICE,
      3
    );

    arbAdd(
      d,
      ELLI_ARB_CLOCK,
      3
    );

    arbAdd(
      d,
      ELLI_ARB_TASK,
      3
    );
  }


  if(nlu.statement){

    if(nlu.personal){

      arbAdd(
        d,
        ELLI_ARB_PERSONAL_CONVERSATION,
        17
      );
    }

    else{

      arbAdd(
        d,
        ELLI_ARB_STATEMENT,
        18
      );
    }
  }


  if(nlu.stablePersonal){

    arbAdd(
      d,
      ELLI_ARB_PERSONAL_MEMORY,
      22
    );
  }


  if(nlu.transientPersonal){

    arbAdd(
      d,
      ELLI_ARB_PERSONAL_CONVERSATION,
      25
    );
  }


  // A fragment should not be confidently forced into web.
  if(nlu.fragment){

    arbAdd(
      d,
      ELLI_ARB_STATEMENT,
      4
    );
  }
}


// =====================================================
// SIGNAL 3: SEMANTIC FRAME V2
// =====================================================

static bool arbSemanticPersonalRelation(
  ElliSemanticRelationKind rel
){

  switch(rel){

    case ELLI_SEM_REL_ATTRIBUTE:
    case ELLI_SEM_REL_EDUCATION_LEVEL:
    case ELLI_SEM_REL_EDUCATION_ACTIVITY:
    case ELLI_SEM_REL_AGE:
    case ELLI_SEM_REL_RELATIONSHIP:
    case ELLI_SEM_REL_PREFERENCE:
    case ELLI_SEM_REL_POSSESSION:
    case ELLI_SEM_REL_RESIDENCE:
    case ELLI_SEM_REL_MEMBERSHIP:
    case ELLI_SEM_REL_OCCUPATION:
    case ELLI_SEM_REL_LANGUAGE:
    case ELLI_SEM_REL_PROJECT:

      return true;

    default:

      return false;
  }
}


static void arbScoreSemantic(
  ElliArbiterDecision& d,
  const ElliSemanticFrame& sem
){

  if(!sem.valid){
    return;
  }


  if(
    sem.queryTarget!=
    ELLI_SEM_QUERY_NONE
  ){

    arbAdd(
      d,
      ELLI_ARB_KNOWLEDGE,
      8
    );
  }


  if(
    sem.subjectKind==
    ELLI_SEM_SUBJECT_USER
  ){

    if(sem.question){

      if(
        arbSemanticPersonalRelation(
          sem.relationKind
        )
      ){

        arbAdd(
          d,
          ELLI_ARB_PERSONAL_MEMORY,
          20
        );
      }

      else{

        arbAdd(
          d,
          ELLI_ARB_PERSONAL_MEMORY,
          5
        );
      }
    }

    else{

      arbAdd(
        d,
        ELLI_ARB_PERSONAL_CONVERSATION,
        8
      );
    }
  }


  // Advice / feasibility / goal reasoning is knowledge even
  // when the sentence talks about the user.
  if(
    sem.question &&
    (
      sem.relationKind==
      ELLI_SEM_REL_CAPABILITY ||

      sem.relationKind==
      ELLI_SEM_REL_GOAL ||

      sem.relationKind==
      ELLI_SEM_REL_INTENTION
    )
  ){

    arbAdd(
      d,
      ELLI_ARB_KNOWLEDGE,
      18
    );
  }


  if(
    !sem.question &&
    sem.subjectKind==
    ELLI_SEM_SUBJECT_USER
  ){

    if(
      sem.relationKind==
      ELLI_SEM_REL_STATE
    ){

      arbAdd(
        d,
        ELLI_ARB_PERSONAL_CONVERSATION,
        16
      );
    }


    if(
      arbSemanticPersonalRelation(
        sem.relationKind
      )
    ){

      arbAdd(
        d,
        ELLI_ARB_PERSONAL_MEMORY,
        12
      );
    }
  }
}


// =====================================================
// DETERMINISTIC SAFETY SIGNALS
// =====================================================

static void arbScoreDeterministic(
  ElliArbiterDecision& d,
  const String& input
){

  String q=
    arbNormalize(
      input
    );


  // Local date/time request detector already contains the
  // semantic safeguards that avoid confusing:
  //
  //   "what time is it"
  //
  // with:
  //
  //   "what is time dilation"
  //
  if(
    detectLocalTemporalRequest(q)
    !=TEMPORAL_NONE
  ){

    arbFloor(
      d,
      ELLI_ARB_CLOCK,
      100
    );
  }


  // A bare STOP is a high-priority runtime interrupt.
  if(
    arbLocalStop(
      q
    )
  ){

    arbFloor(
      d,
      ELLI_ARB_CLOCK,
      100
    );
  }
}


// =====================================================
// WINNER / AMBIGUITY
// =====================================================

static void arbFinalize(
  ElliArbiterDecision& d
){

  ElliArbiterClass best=
    ELLI_ARB_NONE;

  ElliArbiterClass second=
    ELLI_ARB_NONE;

  uint8_t bestScore=0;
  uint8_t secondScore=0;


  for(
    int i=1;
    i<ELLI_ARB_CLASS_COUNT;
    i++
  ){

    uint8_t s=
      d.score[i];


    if(
      s>bestScore
    ){

      second=
        best;

      secondScore=
        bestScore;

      best=
        (ElliArbiterClass)i;

      bestScore=
        s;
    }

    else if(
      s>secondScore
    ){

      second=
        (ElliArbiterClass)i;

      secondScore=
        s;
    }
  }


  d.winner=
    best;

  d.runnerUp=
    second;

  d.winnerScore=
    bestScore;

  d.runnerUpScore=
    secondScore;

  d.gap=

    bestScore>=secondScore

    ?

    bestScore-secondScore

    :

    0;


  if(
    bestScore<40
  ){

    d.winner=
      ELLI_ARB_NONE;

    d.winnerScore=
      0;
  }


  d.decisive=

    d.winner!=ELLI_ARB_NONE

    &&

    (
      (
        arbStrongLocalClass(
          d.winner
        )
        &&
        d.winnerScore>=95
      )

      ||

      d.gap>=15
    );


  // Clarification is a future TAKEOVER behavior.
  // In V1 this flag is diagnostic only.
  d.ambiguous=

    d.winner!=ELLI_ARB_NONE

    &&

    !d.decisive

    &&

    d.winnerScore>=65

    &&

    d.runnerUpScore>=60

    &&

    d.gap<=7;
}


// =====================================================
// PUBLIC ARBITRATION
// =====================================================

ElliArbiterDecision elliArbitrate(
  const String& input
){

  ElliArbiterDecision d;


  ElliNLUFrame nlu=
    elliAnalyzeNLU(
      input
    );


  ElliSemanticFrame sem=
    elliBuildSemanticFrame(
      input
    );


  int legacy=
    detectUtteranceIntent(
      input
    );


  d.legacyIntent=
    legacy;

  d.nluConfidence=
    nlu.confidence;

  d.semanticConfidence=
    sem.confidence;


  arbScoreLegacyIntent(
    d,
    legacy,
    nlu
  );


  arbScoreNLU(
    d,
    nlu
  );


  arbScoreSemantic(
    d,
    sem
  );


  arbScoreDeterministic(
    d,
    input
  );


  arbFinalize(
    d
  );


  return d;
}


// =====================================================
// NAMES
// =====================================================

const char* elliArbiterClassName(
  ElliArbiterClass cls
){

  switch(cls){

    case ELLI_ARB_CLOCK:
      return "CLOCK / LOCAL CONTROL";

    case ELLI_ARB_DEVICE:
      return "DEVICE ACTION";

    case ELLI_ARB_TASK:
      return "TASK / REMINDER";

    case ELLI_ARB_ROUTINE:
      return "ROUTINE";

    case ELLI_ARB_PERSONAL_MEMORY:
      return "PERSONAL MEMORY";

    case ELLI_ARB_SUPPORT:
      return "SUPPORT";

    case ELLI_ARB_KNOWLEDGE:
      return "KNOWLEDGE";

    case ELLI_ARB_CHAT:
      return "CASUAL CHAT";

    case ELLI_ARB_PERSONAL_CONVERSATION:
      return "PERSONAL CONVERSATION";

    case ELLI_ARB_STATEMENT:
      return "GENERAL STATEMENT";

    default:
      return "NONE";
  }
}


// =====================================================
// COMPACT SHADOW TELEMETRY
// =====================================================

void printElliArbiterShadow(
  const ElliArbiterDecision& d
){

  Serial.print(
    "[ARBITER SHADOW] winner="
  );

  Serial.print(
    elliArbiterClassName(
      d.winner
    )
  );

  Serial.print(
    " "
  );

  Serial.print(
    d.winnerScore
  );


  Serial.print(
    " | runner="
  );

  Serial.print(
    elliArbiterClassName(
      d.runnerUp
    )
  );

  Serial.print(
    " "
  );

  Serial.print(
    d.runnerUpScore
  );


  Serial.print(
    " | gap="
  );

  Serial.print(
    d.gap
  );


  Serial.print(
    " | ambiguous="
  );

  Serial.println(
    d.ambiguous
    ?
    "YES"
    :
    "NO"
  );
}


// =====================================================
// FULL DEBUG
// =====================================================

void printElliArbiterDecision(
  const String& input
){

  ElliArbiterDecision d=
    elliArbitrate(
      input
    );


  ElliNLUFrame nlu=
    elliAnalyzeNLU(
      input
    );


  ElliSemanticFrame sem=
    elliBuildSemanticFrame(
      input
    );


  Serial.println();

  Serial.println(
    "========== ELLI INTENT ARBITER V1 =========="
  );


  Serial.print(
    "Input          : "
  );

  Serial.println(
    input
  );


  Serial.print(
    "Legacy intent  : "
  );

  Serial.println(
    utteranceIntentName(
      d.legacyIntent
    )
  );


  Serial.print(
    "NLU kind       : "
  );

  Serial.println(
    elliSentenceKindName(
      nlu.kind
    )
  );


  Serial.print(
    "NLU confidence : "
  );

  Serial.println(
    d.nluConfidence
  );


  Serial.print(
    "Semantic rel   : "
  );

  Serial.println(
    elliSemanticRelationName(
      sem.relationKind
    )
  );


  Serial.print(
    "Semantic label : "
  );

  Serial.println(
    sem.relationLabel
  );


  Serial.print(
    "Semantic conf  : "
  );

  Serial.println(
    d.semanticConfidence
  );


  Serial.println(
    "---------------------------------------------"
  );


  for(
    int i=1;
    i<ELLI_ARB_CLASS_COUNT;
    i++
  ){

    Serial.print(
      elliArbiterClassName(
        (ElliArbiterClass)i
      )
    );


    int pad=
      24-
      strlen(
        elliArbiterClassName(
          (ElliArbiterClass)i
        )
      );


    while(pad-->0){
      Serial.print(" ");
    }


    Serial.print(
      ": "
    );

    Serial.println(
      d.score[i]
    );
  }


  Serial.println(
    "---------------------------------------------"
  );


  Serial.print(
    "WINNER         : "
  );

  Serial.print(
    elliArbiterClassName(
      d.winner
    )
  );

  Serial.print(
    " / "
  );

  Serial.println(
    d.winnerScore
  );


  Serial.print(
    "RUNNER-UP      : "
  );

  Serial.print(
    elliArbiterClassName(
      d.runnerUp
    )
  );

  Serial.print(
    " / "
  );

  Serial.println(
    d.runnerUpScore
  );


  Serial.print(
    "GAP            : "
  );

  Serial.println(
    d.gap
  );


  Serial.print(
    "AMBIGUOUS      : "
  );

  Serial.println(
    d.ambiguous
    ?
    "YES"
    :
    "NO"
  );


  Serial.print(
    "DECISIVE       : "
  );

  Serial.println(
    d.decisive
    ?
    "YES"
    :
    "NO"
  );


  Serial.print(
    "MODE           : "
  );

  Serial.println(
    KIRA_INTENT_ARBITER_TAKEOVER
    ?
    "TAKEOVER"
    :
    "SHADOW ONLY"
  );


  Serial.println(
    "============================================="
  );
}


// =====================================================
// ACTUAL ROUTE COMPARISON
// =====================================================

static ElliArbiterClass arbClassFromActualRoute(
  String route
){

  route.trim();


  if(
    route.startsWith("LOCAL ALARM") ||
    route.startsWith("LOCAL TIMER") ||
    route.startsWith("LOCAL STOPWATCH") ||
    route.startsWith("LOCAL CLOCK") ||
    route=="LOCAL TIME" ||
    route=="LOCAL CONTROL"
  ){

    return
      ELLI_ARB_CLOCK;
  }


  if(
    route=="LOCAL DEVICE"
  ){

    return
      ELLI_ARB_DEVICE;
  }


  if(
    route=="PERSONAL TASK"
  ){

    return
      ELLI_ARB_TASK;
  }


  if(
    route=="PERSONAL ROUTINE"
  ){

    return
      ELLI_ARB_ROUTINE;
  }


  if(
    route=="PERSONAL MEMORY" ||
    route=="PERSONAL MEMORY RECALL"
  ){

    return
      ELLI_ARB_PERSONAL_MEMORY;
  }


  if(
    route=="SUPPORT / MOTIVATION"
  ){

    return
      ELLI_ARB_SUPPORT;
  }


  if(
    route=="V1 UNIVERSAL QUERY" ||
    route=="SEMANTIC CORE" ||
    route=="WEB"
  ){

    return
      ELLI_ARB_KNOWLEDGE;
  }


  if(
    route=="LOCAL CHAT"
  ){

    return
      ELLI_ARB_CHAT;
  }


  if(
    route=="PERSONAL CONVERSATION"
  ){

    return
      ELLI_ARB_PERSONAL_CONVERSATION;
  }


  if(
    route=="LOCAL STATEMENT"
  ){

    return
      ELLI_ARB_STATEMENT;
  }


  // Utilities, diagnostics, pending clarification, calculator,
  // converter, calendar, music, system status, etc. are intentionally
  // not judged in V1 shadow comparison.
  return
    ELLI_ARB_NONE;
}


void elliArbiterCompareRoute(
  const ElliArbiterDecision& d,
  const String& actualRoute
){

  ElliArbiterClass actual=
    arbClassFromActualRoute(
      actualRoute
    );


  if(
    d.winner==ELLI_ARB_NONE
  ){

    return;
  }


  // A very useful shadow-mode regression alarm:
  //
  // If the current router falls all the way to LOCAL CLARIFY while
  // the arbiter has a strong interpretation, print it explicitly.
  // This is exactly how recurring-reminder gaps become easy to spot.
  if(
    actualRoute=="LOCAL CLARIFY" &&
    d.winnerScore>=80
  ){

    Serial.print(
      "[ARBITER WARNING] Router fell to CLARIFY, but arbiter strongly proposed "
    );

    Serial.print(
      elliArbiterClassName(
        d.winner
      )
    );

    Serial.print(
      " / "
    );

    Serial.println(
      d.winnerScore
    );


    return;
  }


  if(
    actual==ELLI_ARB_NONE
  ){

    return;
  }


  Serial.print(
    "[ARBITER COMPARE] actual="
  );

  Serial.print(
    elliArbiterClassName(
      actual
    )
  );


  Serial.print(
    " | proposed="
  );

  Serial.print(
    elliArbiterClassName(
      d.winner
    )
  );


  if(
    actual==
    d.winner
  ){

    Serial.println(
      " | MATCH"
    );
  }

  else{

    Serial.print(
      " | MISMATCH | gap="
    );

    Serial.println(
      d.gap
    );
  }
}
