#include <Arduino.h>

#include "elli_response_composer.h"
#include "elli_nlu.h"


String normalizeInput(String s);


// =====================================================
// SMALL HELPERS
// =====================================================

static String rcSentence(String s){

  s.trim();


  while(
    s.length() &&
    (
      s.endsWith(".") ||
      s.endsWith("!") ||
      s.endsWith("?")
    )
  ){

    s.remove(
      s.length()-1
    );

    s.trim();
  }


  if(!s.length()){
    return "";
  }


  s.setCharAt(
    0,
    toupper(
      (unsigned char)s[0]
    )
  );


  return
    s+
    ".";
}


static String rcLowerInitial(String s){

  s.trim();


  if(s.length()){

    s.setCharAt(
      0,
      tolower(
        (unsigned char)s[0]
      )
    );
  }


  return s;
}


static uint8_t rcPickDifferent(
  uint8_t count,
  uint8_t& last
){

  if(count<=1){

    last=0;

    return 0;
  }


  uint8_t pick=
    (uint8_t)random(
      count
    );


  if(
    pick==last
  ){

    pick=
      (uint8_t)(
        (pick+1)%
        count
      );
  }


  last=pick;

  return pick;
}


static String rcVariant(
  const char* const values[],
  size_t count,
  uint8_t& last
){

  if(!count){
    return "";
  }


  return
    String(
      values[
        rcPickDifferent(
          (uint8_t)count,
          last
        )
      ]
    );
}


static String rcRelationSurface(
  String relation
){

  relation=
    normalizeInput(
      relation
    );


  relation.replace(
    "_",
    " "
  );


  return relation;
}


static bool rcContainsAny(
  String q,
  const char* const words[],
  size_t count
){

  q=
    normalizeInput(
      q
    );


  String padded=
    " "+
    q+
    " ";


  for(
    size_t i=0;
    i<count;
    i++
  ){

    String w=
      normalizeInput(
        String(words[i])
      );


    if(
      padded.indexOf(
        " "+
        w+
        " "
      )>=0
      ||
      q.indexOf(w)>=0
    ){

      return true;
    }
  }


  return false;
}


static String rcCopulaForFrame(
  const ElliSemanticFrame& f,
  bool plural=false
){

  if(
    f.tense==
    ELLI_TENSE_PAST
  ){

    return
      plural
      ?
      "were"
      :
      "was";
  }


  if(
    f.tense==
    ELLI_TENSE_FUTURE
  ){

    return
      "will be";
  }


  return
    plural
    ?
    "are"
    :
    "is";
}


static String rcYouBeForFrame(
  const ElliSemanticFrame& f
){

  if(
    f.tense==
    ELLI_TENSE_PAST
  ){

    return
      "You were ";
  }


  if(
    f.tense==
    ELLI_TENSE_FUTURE
  ){

    return
      "You'll be ";
  }


  return
    "You're ";
}


static String rcYouHaveForFrame(
  const ElliSemanticFrame& f
){

  if(
    f.tense==
    ELLI_TENSE_PAST
  ){

    return
      "You had ";
  }


  if(
    f.tense==
    ELLI_TENSE_FUTURE
  ){

    return
      "You'll have ";
  }


  return
    "You have ";
}


static String rcWithTime(
  String sentence,
  const ElliSemanticFrame& f
){

  sentence.trim();


  if(
    f.timeText.length() &&
    sentence.indexOf(
      f.timeText
    )<0
  ){

    sentence+=
      " "+
      f.timeText;
  }


  return sentence;
}


// =====================================================
// SEMANTIC PROPOSITION
// =====================================================

String elliSemanticProposition(
  const ElliSemanticFrame& f
){

  if(
    !f.valid
  ){

    return
      rcSentence(
        elliPerspectiveToUser(
          f.normalized
        )
      );
  }


  // ---------------------------------------------------
  // USER RELATIONSHIP
  //
  // "sai and jagruti are my sisters"
  // -> "Sai and Jagruti are your sisters."
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    f.relationKind==
    ELLI_SEM_REL_RELATIONSHIP &&
    f.value.length()
  ){

    String relation=
      rcRelationSurface(
        f.relationSurface
      );


    String s=
      f.value+
      " "+
      rcCopulaForFrame(
        f,
        f.pluralValue ||
        relation.endsWith("s")
      )+
      " your "+
      relation;


    return
      rcSentence(
        s
      );
  }


  // ---------------------------------------------------
  // EDUCATION LEVEL
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    f.relationKind==
    ELLI_SEM_REL_EDUCATION_LEVEL &&
    f.value.length()
  ){

    // Preserve possessive grammar:
    //
    //   my grade is 10th
    //   -> Your grade is 10th.
    //
    // while copular-location grammar becomes:
    //
    //   i am in 10th grade
    //   -> You're in 10th grade.

    if(
      f.normalized.startsWith(
        "my "
      )
      &&
      f.relationSurface.length()
    ){

      return
        rcSentence(
          "Your "+
          rcRelationSurface(
            f.relationSurface
          )+
          " "+
          rcCopulaForFrame(
            f,
            false
          )+
          " "+
          f.value
        );
    }


    String s=
      rcYouBeForFrame(f)+
      "in "+
      f.value;


    s=
      rcWithTime(
        s,
        f
      );


    return
      rcSentence(
        s
      );
  }


  // ---------------------------------------------------
  // AGE
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    f.relationKind==
    ELLI_SEM_REL_AGE &&
    f.value.length()
  ){

    if(
      f.normalized.startsWith(
        "my "
      )
      &&
      f.relationSurface.length()
    ){

      return
        rcSentence(
          "Your "+
          rcRelationSurface(
            f.relationSurface
          )+
          " "+
          rcCopulaForFrame(
            f,
            false
          )+
          " "+
          f.value
        );
    }


    String s=
      rcYouBeForFrame(f)+
      f.value;


    s=
      rcWithTime(
        s,
        f
      );


    return
      rcSentence(
        s
      );
  }


  // ---------------------------------------------------
  // POSSESSIVE ATTRIBUTE
  //
  // my favorite game is minecraft
  // -> Your favorite game is Minecraft.
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    (
      f.relationKind==
      ELLI_SEM_REL_ATTRIBUTE ||

      f.relationKind==
      ELLI_SEM_REL_PROJECT ||

      f.relationKind==
      ELLI_SEM_REL_OCCUPATION ||

      f.relationKind==
      ELLI_SEM_REL_LANGUAGE
    )
    &&
    f.relationSurface.length() &&
    f.value.length()
  ){

    String s=
      "Your "+
      rcRelationSurface(
        f.relationSurface
      )+
      " "+
      rcCopulaForFrame(
        f,
        false
      )+
      " "+
      f.value;


    return
      rcSentence(
        s
      );
  }


  // ---------------------------------------------------
  // PREFERENCE
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    f.relationKind==
    ELLI_SEM_REL_PREFERENCE &&
    f.value.length()
  ){

    // Possessive "favorite/favourite" is an attribute statement,
    // not equivalent to the weaker proposition "you like X".
    //
    //   my favorite game is minecraft
    //   -> Your favorite game is Minecraft.
    if(
      f.normalized.startsWith(
        "my "
      )
      &&
      (
        f.relationSurface.indexOf(
          "favorite"
        )>=0
        ||
        f.relationSurface.indexOf(
          "favourite"
        )>=0
      )
    ){

      return
        rcSentence(
          "Your "+
          rcRelationSurface(
            f.relationSurface
          )+
          " "+
          rcCopulaForFrame(
            f,
            false
          )+
          " "+
          f.value
        );
    }

    if(
      f.relationLabel=="DISLIKE" ||
      f.negated
    ){

      return
        rcSentence(
          "You don't like "+
          f.value
        );
    }


    if(
      f.relationLabel=="PREFER"
    ){

      return
        rcSentence(
          "You prefer "+
          f.value
        );
    }


    return
      rcSentence(
        "You like "+
        f.value
      );
  }


  // ---------------------------------------------------
  // POSSESSION
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    f.relationKind==
    ELLI_SEM_REL_POSSESSION &&
    f.value.length()
  ){

    return
      rcSentence(
        rcYouHaveForFrame(f)+
        f.value
      );
  }


  // ---------------------------------------------------
  // RESIDENCE
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    f.relationKind==
    ELLI_SEM_REL_RESIDENCE &&
    f.value.length()
  ){

    String verb="live";


    if(
      f.tense==
      ELLI_TENSE_PAST
    ){

      verb="lived";
    }


    String s=
      "You "+
      verb+
      " in "+
      f.value;


    return
      rcSentence(
        s
      );
  }


  // ---------------------------------------------------
  // MEMBERSHIP / LOCATION
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    (
      f.relationKind==
      ELLI_SEM_REL_MEMBERSHIP ||
      f.relationKind==
      ELLI_SEM_REL_LOCATION
    )
    &&
    f.value.length()
  ){

    String s=
      rcYouBeForFrame(f)+
      "in "+
      f.value;


    s=
      rcWithTime(
        s,
        f
      );


    return
      rcSentence(
        s
      );
  }


  // ---------------------------------------------------
  // EDUCATION ACTIVITY / ACTIVITY
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    (
      f.relationKind==
      ELLI_SEM_REL_EDUCATION_ACTIVITY ||
      f.relationKind==
      ELLI_SEM_REL_ACTIVITY
    )
  ){

    return
      rcSentence(
        elliPerspectiveToUser(
          f.normalized
        )
      );
  }


  // ---------------------------------------------------
  // GOAL / INTENTION / CAPABILITY
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER &&
    (
      f.relationKind==
      ELLI_SEM_REL_GOAL ||
      f.relationKind==
      ELLI_SEM_REL_INTENTION ||
      f.relationKind==
      ELLI_SEM_REL_CAPABILITY
    )
  ){

    return
      rcSentence(
        elliPerspectiveToUser(
          f.normalized
        )
      );
  }


  // ---------------------------------------------------
  // STATE / UNKNOWN PERSONAL FACT
  // ---------------------------------------------------

  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER
  ){

    return
      rcSentence(
        elliPerspectiveToUser(
          f.normalized
        )
      );
  }


  // Generic fallback.
  return
    rcSentence(
      f.normalized
    );
}


// =====================================================
// RESPONSE PLAN
// =====================================================

static ElliResponseTone rcToneForFrame(
  const ElliSemanticFrame& f
){

  String q=
    f.normalized;


  const char* const lowWords[]={

    "tired",
    "exhausted",
    "drained",
    "sleepy",
    "stressed",
    "worried",
    "frustrated",
    "sad",
    "upset",
    "overwhelmed",
    "lonely"
  };


  if(
    rcContainsAny(
      q,
      lowWords,
      sizeof(lowWords)/
      sizeof(lowWords[0])
    )
  ){

    return
      ELLI_TONE_EMPATHETIC;
  }


  const char* const positiveWords[]={

    "happy",
    "excited",
    "great",
    "awesome",
    "proud",
    "relaxed",
    "amazing"
  };


  if(
    rcContainsAny(
      q,
      positiveWords,
      sizeof(positiveWords)/
      sizeof(positiveWords[0])
    )
  ){

    return
      ELLI_TONE_POSITIVE;
  }


  if(
    f.relationKind==
    ELLI_SEM_REL_PREFERENCE
  ){

    return
      ELLI_TONE_WARM;
  }


  return
    ELLI_TONE_NEUTRAL;
}


static String rcPersonalPrefix(
  ElliResponseTone tone
){

  static uint8_t lastNeutral=255;
  static uint8_t lastWarm=255;
  static uint8_t lastEmpathy=255;
  static uint8_t lastPositive=255;


  if(
    tone==
    ELLI_TONE_EMPATHETIC
  ){

    const char* const a[]={

      "I hear you — ",
      "That sounds like a lot — ",
      "Yeah, I get you — ",
      "Sounds like it's been one of those days — ",
      "Oof, I hear you — "
    };


    return
      rcVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastEmpathy
      );
  }


  if(
    tone==
    ELLI_TONE_POSITIVE
  ){

    const char* const a[]={

      "Nice — ",
      "Love that — ",
      "That's good to hear — ",
      "Yooo, nice — ",
      "Glad to hear it — "
    };


    return
      rcVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastPositive
      );
  }


  if(
    tone==
    ELLI_TONE_WARM
  ){

    const char* const a[]={

      "Fair enough — ",
      "Makes sense — ",
      "I get that — ",
      "Yeah, I can see that — ",
      "That tracks — "
    };


    return
      rcVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastWarm
      );
  }


  const char* const a[]={

    "Got you — ",
    "Makes sense — ",
    "I follow — ",
    "Alright — ",
    "Okay, I see — ",
    "Yeah, got it — "
  };


  return
    rcVariant(
      a,
      sizeof(a)/sizeof(a[0]),
      lastNeutral
    );
}


static String rcEmpathicFollowup(
  const ElliSemanticFrame& f
){

  static uint8_t last=255;


  const char* const lowEnergy[]={

    "tired",
    "exhausted",
    "drained",
    "sleepy"
  };


  if(
    rcContainsAny(
      f.normalized,
      lowEnergy,
      sizeof(lowEnergy)/
      sizeof(lowEnergy[0])
    )
  ){

    const char* const a[]={

      " Hope you get a chance to recharge a bit.",
      " What wore you out most?",
      " Hopefully the rest of the day is easier on you.",
      " A little downtime might feel pretty good.",
      " Want to tell me what made the day so tiring?"
    };


    return
      rcVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        last
      );
  }


  const char* const a[]={

    " Want to talk about what happened?",
    " What's been the hardest part?",
    " If you want, tell me a bit more.",
    " Hope things ease up a little from here.",
    " I'm here if you want to unpack it."
  };


  return
    rcVariant(
      a,
      sizeof(a)/sizeof(a[0]),
      last
    );
}


static String rcPositiveFollowup(){

  static uint8_t last=255;


  const char* const a[]={

    " What made it so good?",
    " What's been the best part?",
    " Sounds like something went right.",
    " Nice energy to have.",
    " Hope that keeps going."
  };


  return
    rcVariant(
      a,
      sizeof(a)/sizeof(a[0]),
      last
    );
}


// =====================================================
// PERSONAL CONVERSATION
// =====================================================

String elliComposePersonalSemantic(
  String input
){

  ElliSemanticFrame f=
    elliBuildSemanticFrame(
      input
    );


  // Semantic extraction is deliberately conservative.
  // If it is weak, keep the proven V7.1 composer.
  if(
    !f.valid ||
    f.confidence<60
  ){

    return
      elliNaturalPersonalAcknowledgement(
        input
      );
  }


  String core=
    elliSemanticProposition(
      f
    );


  ElliResponseTone tone=
    rcToneForFrame(
      f
    );


  String answer=
    rcPersonalPrefix(
      tone
    )+
    core;


  if(
    tone==
    ELLI_TONE_EMPATHETIC
  ){

    answer+=
      rcEmpathicFollowup(
        f
      );
  }

  else if(
    tone==
    ELLI_TONE_POSITIVE
  ){

    answer+=
      rcPositiveFollowup();
  }


  return answer;
}


// =====================================================
// MEMORY ACKNOWLEDGEMENTS
// =====================================================

String elliComposeMemorySavedSemantic(
  String input,
  bool explicitSave
){

  ElliSemanticFrame f=
    elliBuildSemanticFrame(
      input
    );


  if(
    !f.valid ||
    f.confidence<55
  ){

    return
      elliNaturalMemorySavedReply(
        input,
        explicitSave
      );
  }


  String core=
    elliSemanticProposition(
      f
    );


  static uint8_t lastExplicit=255;
  static uint8_t lastAuto=255;


  if(explicitSave){

    const char* const prefixes[]={

      "Absolutely — ",
      "Done — ",
      "Yep — ",
      "Sure — ",
      "Alright — "
    };


    const char* const suffixes[]={

      " I'll remember that.",
      " That's saved.",
      " I'll keep that in memory.",
      " I've stored that for later.",
      " I'll keep that context for future conversations."
    };


    uint8_t i=
      rcPickDifferent(
        (uint8_t)(
          sizeof(prefixes)/
          sizeof(prefixes[0])
        ),
        lastExplicit
      );


    return
      String(prefixes[i])+
      core+
      String(
        suffixes[
          i%
          (
            sizeof(suffixes)/
            sizeof(suffixes[0])
          )
        ]
      );
  }


  const char* const prefixes[]={

    "Thanks for telling me — ",
    "Okay — ",
    "That helps — ",
    "Understood — ",
    "Nice, I'll keep that context — "
  };


  const char* const suffixes[]={

    " I'll remember it.",
    " That's now part of your local profile.",
    " I'll use that context when it's relevant.",
    " I'll keep it in mind for later.",
    " That's stored locally."
  };


  uint8_t i=
    rcPickDifferent(
      (uint8_t)(
        sizeof(prefixes)/
        sizeof(prefixes[0])
      ),
      lastAuto
    );


  return
    String(prefixes[i])+
    core+
    String(
      suffixes[
        i%
        (
          sizeof(suffixes)/
          sizeof(suffixes[0])
        )
      ]
    );
}


String elliComposeMemoryKnownSemantic(
  String input
){

  ElliSemanticFrame f=
    elliBuildSemanticFrame(
      input
    );


  if(
    !f.valid ||
    f.confidence<55
  ){

    return
      elliNaturalMemoryKnownReply(
        input
      );
  }


  String core=
    elliSemanticProposition(
      f
    );


  static uint8_t last=255;


  const char* const prefixes[]={

    "Yep — ",
    "Right — ",
    "I remember — ",
    "Exactly — ",
    "Already got that one — "
  };


  const char* const suffixes[]={

    " That's already saved.",
    " I remember.",
    " That's in your local profile.",
    " I've got that in memory.",
    " No need to tell me twice."
  };


  uint8_t i=
    rcPickDifferent(
      (uint8_t)(
        sizeof(prefixes)/
        sizeof(prefixes[0])
      ),
      last
    );


  return
    String(prefixes[i])+
    core+
    String(
      suffixes[
        i%
        (
          sizeof(suffixes)/
          sizeof(suffixes[0])
        )
      ]
    );
}


String elliComposeMemoryUnsavedSemantic(
  String input
){

  ElliSemanticFrame f=
    elliBuildSemanticFrame(
      input
    );


  if(
    !f.valid ||
    f.confidence<55
  ){

    return
      elliNaturalMemoryUnsavedReply(
        input
      );
  }


  String core=
    elliSemanticProposition(
      f
    );


  static uint8_t last=255;


  const char* const prefixes[]={

    "Thanks for telling me — ",
    "I follow — ",
    "Okay — ",
    "Makes sense — ",
    "Alright — "
  };


  const char* const suffixes[]={

    " I understand it, but I won't save it permanently unless you ask.",
    " I can use that in this conversation; say 'remember that...' if you want it stored.",
    " I'm following, but permanent memory stays off unless you ask me to save it.",
    " I'll understand that for now without silently storing it.",
    " If you want that kept for later, just ask me to remember it."
  };


  uint8_t i=
    rcPickDifferent(
      (uint8_t)(
        sizeof(prefixes)/
        sizeof(prefixes[0])
      ),
      last
    );


  return
    String(prefixes[i])+
    core+
    String(
      suffixes[
        i%
        (
          sizeof(suffixes)/
          sizeof(suffixes[0])
        )
      ]
    );
}


// =====================================================
// MEMORY RECALL
// =====================================================

static String rcRecallFact(String fact){

  ElliSemanticFrame f=
    elliBuildSemanticFrame(
      fact
    );


  if(
    f.valid &&
    f.confidence>=55
  ){

    return
      elliSemanticProposition(
        f
      );
  }


  return
    rcSentence(
      elliPerspectiveToUser(
        fact
      )
    );
}


String elliComposeMemoryRecallSemantic(
  String query,
  String firstFact,
  String secondFact,
  String thirdFact
){

  query=
    normalizeInput(
      query
    );


  String first=
    rcRecallFact(
      firstFact
    );


  String second=

    secondFact.length()

    ?

    rcRecallFact(
      secondFact
    )

    :

    "";


  String third=

    thirdFact.length()

    ?

    rcRecallFact(
      thirdFact
    )

    :

    "";


  bool explicitRemember=

    query.indexOf(
      "do you remember"
    )>=0

    ||

    query.indexOf(
      "did i tell you"
    )>=0;


  bool knowledgeCheck=

    query.startsWith(
      "do you know my "
    )

    ||

    query.startsWith(
      "do you know who my "
    )

    ||

    query.startsWith(
      "do you know what my "
    )

    ||

    query.startsWith(
      "do you know where my "
    )

    ||

    query.startsWith(
      "do you know when my "
    );


  bool tellMeAbout=

    query.startsWith(
      "tell me about"
    )

    ||

    query.indexOf(
      "what do you know about"
    )>=0

    ||

    query.indexOf(
      "what did i tell you about"
    )>=0;


  bool direct=

    query.startsWith("who ") ||
    query.startsWith("what ") ||
    query.startsWith("where ") ||
    query.startsWith("when ") ||
    query.startsWith("which ") ||
    query.startsWith("how old ") ||
    query.startsWith("how many ") ||
    query.startsWith("name my ");


  String answer="";


  if(explicitRemember){

    answer=
      "Yes. I remember that "+
      rcLowerInitial(
        first
      );
  }

  else if(knowledgeCheck){

    answer=
      "Yes. From what you've told me, "+
      rcLowerInitial(
        first
      );
  }

  else if(tellMeAbout){

    answer=
      "You told me that "+
      rcLowerInitial(
        first
      );
  }

  else if(direct){

    answer=
      first;
  }

  else{

    answer=
      "Here's what I remember: "+
      first;
  }


  if(second.length()){

    answer+=" ";

    answer+=
      second;
  }


  if(third.length()){

    answer+=" ";

    answer+=
      third;
  }


  return answer;
}


// =====================================================
// DEBUG
// =====================================================

void printElliResponsePlan(
  String input
){

  ElliSemanticFrame f=
    elliBuildSemanticFrame(
      input
    );


  String core=
    elliSemanticProposition(
      f
    );


  String finalResponse=
    elliComposePersonalSemantic(
      input
    );


  Serial.println();

  Serial.println(
    "========== ELLI RESPONSE COMPOSER V1 =========="
  );


  Serial.print(
    "Input      : "
  );

  Serial.println(
    f.normalized
  );


  Serial.print(
    "Relation   : "
  );

  Serial.println(
    f.relationLabel
  );


  Serial.print(
    "Core       : "
  );

  Serial.println(
    core
  );


  Serial.print(
    "Final      : "
  );

  Serial.println(
    finalResponse
  );


  Serial.print(
    "Confidence : "
  );

  Serial.println(
    f.confidence
  );


  Serial.println(
    "==============================================="
  );
}
