#include <Arduino.h>

#include "elli_semantic_v2.h"


// Existing shared normalizer in kira_brain.ino.
String normalizeInput(String s);


// =====================================================
// SMALL TEXT HELPERS
// =====================================================

static bool sfBoundedToken(
  String q,
  const String& token
){

  q=
    " "+
    normalizeInput(q)+
    " ";


  return
    q.indexOf(
      " "+
      token+
      " "
    )>=0;
}


static String sfTrimPunctuation(String s){

  s.trim();


  while(
    s.length() &&
    (
      s.endsWith(".") ||
      s.endsWith("?") ||
      s.endsWith("!") ||
      s.endsWith(",") ||
      s.endsWith(";") ||
      s.endsWith(":")
    )
  ){

    s.remove(
      s.length()-1
    );

    s.trim();
  }


  return s;
}


static String sfUpperRelationLabel(String s){

  s=
    normalizeInput(
      s
    );


  String out="";
  bool underscore=false;


  for(
    size_t i=0;
    i<s.length();
    i++
  ){

    char c=s[i];


    if(
      isalnum(
        (unsigned char)c
      )
    ){

      out+=
        (char)toupper(
          (unsigned char)c
        );

      underscore=false;
    }

    else if(
      out.length() &&
      !underscore
    ){

      out+="_";
      underscore=true;
    }
  }


  while(
    out.endsWith("_")
  ){

    out.remove(
      out.length()-1
    );
  }


  if(!out.length()){
    out="UNKNOWN";
  }


  return out;
}


static String sfLastWord(String s){

  s=
    normalizeInput(
      s
    );


  int p=
    s.lastIndexOf(
      ' '
    );


  if(p<0){
    return s;
  }


  return
    s.substring(
      p+1
    );
}


static bool sfContainsAny(
  String q,
  const char* const values[],
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

    String v=
      normalizeInput(
        String(values[i])
      );


    if(
      !v.length()
    ){
      continue;
    }


    if(
      padded.indexOf(
        " "+
        v+
        " "
      )>=0
      ||
      q.indexOf(v)>=0
    ){

      return true;
    }
  }


  return false;
}


static String sfRemoveSuffixPhrase(
  String text,
  String suffix
){

  text=
    sfTrimPunctuation(
      text
    );

  suffix=
    sfTrimPunctuation(
      suffix
    );


  if(
    suffix.length() &&
    text.endsWith(
      suffix
    )
  ){

    text.remove(
      text.length()-
      suffix.length()
    );

    text.trim();
  }


  return text;
}


static String sfAfterPrefix(
  const String& q,
  const String& prefix
){

  if(
    !q.startsWith(
      prefix
    )
  ){

    return "";
  }


  String value=
    q.substring(
      prefix.length()
    );


  value.trim();

  return value;
}


static int sfFindFirstOf(
  const String& q,
  const char* const terms[],
  size_t count,
  String& found
){

  int best=-1;
  found="";


  for(
    size_t i=0;
    i<count;
    i++
  ){

    String term=
      String(
        terms[i]
      );


    int p=
      q.indexOf(
        term
      );


    if(
      p>=0 &&
      (
        best<0 ||
        p<best
      )
    ){

      best=p;
      found=term;
    }
  }


  return best;
}


// =====================================================
// TIME / QUERY TARGET
// =====================================================

static String sfExtractTimeText(String q){

  q=
    normalizeInput(
      q
    );


  const char* const exactish[]={

    "right now",
    "at the moment",
    "currently",
    "today",
    "tonight",
    "yesterday",
    "tomorrow",

    "last year",
    "last month",
    "last week",
    "last night",

    "next year",
    "next month",
    "next week",

    "this year",
    "this month",
    "this week",
    "this morning",
    "this afternoon",
    "this evening"
  };


  for(
    size_t i=0;
    i<
    sizeof(exactish)/
    sizeof(exactish[0]);
    i++
  ){

    String t=
      String(
        exactish[i]
      );


    if(
      q.indexOf(t)>=0
    ){

      return t;
    }
  }


  // Open-ended duration/since phrases.
  const char* const starters[]={

    " for ",
    " since "
  };


  for(
    size_t i=0;
    i<
    sizeof(starters)/
    sizeof(starters[0]);
    i++
  ){

    int p=
      q.indexOf(
        starters[i]
      );


    if(p>=0){

      String t=
        q.substring(
          p+1
        );


      t.trim();


      // Keep the modifier compact.
      if(
        t.length()>48
      ){

        t=
          t.substring(
            0,
            48
          );
      }


      return t;
    }
  }


  return "";
}


static ElliSemanticQueryTarget sfQueryTarget(
  String q,
  String& questionWord
){

  q=
    normalizeInput(
      q
    );


  questionWord="";


  if(
    q.startsWith("who ")
  ){

    questionWord="who";

    return
      ELLI_SEM_QUERY_PERSON;
  }


  if(
    q.startsWith("where ")
  ){

    questionWord="where";

    return
      ELLI_SEM_QUERY_PLACE;
  }


  if(
    q.startsWith("when ")
  ){

    questionWord="when";

    return
      ELLI_SEM_QUERY_TIME;
  }


  if(
    q.startsWith("why ")
  ){

    questionWord="why";

    return
      ELLI_SEM_QUERY_REASON;
  }


  if(
    q.startsWith("how many ") ||
    q.startsWith("how much ")
  ){

    questionWord="how";

    return
      ELLI_SEM_QUERY_QUANTITY;
  }


  if(
    q.startsWith("how ")
  ){

    questionWord="how";

    return
      ELLI_SEM_QUERY_METHOD;
  }


  if(
    q.startsWith("which ")
  ){

    questionWord="which";

    return
      ELLI_SEM_QUERY_CHOICE;
  }


  if(
    q.startsWith("what ")
  ){

    questionWord="what";

    return
      ELLI_SEM_QUERY_VALUE;
  }


  // Yes/no / modal questions.
  const char* const booleanStarts[]={

    "is ","are ","was ","were ",
    "do ","does ","did ",
    "have ","has ","had ",
    "can ","could ","should ","would ",
    "will ","may ","might ","must "
  };


  for(
    size_t i=0;
    i<
    sizeof(booleanStarts)/
    sizeof(booleanStarts[0]);
    i++
  ){

    if(
      q.startsWith(
        booleanStarts[i]
      )
    ){

      return
        ELLI_SEM_QUERY_BOOLEAN;
    }
  }


  return
    ELLI_SEM_QUERY_NONE;
}


// =====================================================
// SEMANTIC CLASSIFIERS
// =====================================================

static bool sfLooksEducationLevel(String value){

  value=
    normalizeInput(
      value
    );


  const char* const markers[]={

    "grade",
    "class",
    "standard",
    "std",
    "school year",
    "year level"
  };


  return
    sfContainsAny(
      value,
      markers,
      sizeof(markers)/
      sizeof(markers[0])
    );
}


static bool sfLooksAge(String value){

  value=
    normalizeInput(
      value
    );


  return
    value.indexOf("years old")>=0 ||
    value.indexOf("year old")>=0 ||
    value.indexOf("age ")>=0;
}


static bool sfLooksMembership(String value){

  value=
    normalizeInput(
      value
    );


  const char* const markers[]={

    "club",
    "team",
    "group",
    "society",
    "association",
    "committee",
    "classroom",
    "batch"
  };


  return
    sfContainsAny(
      value,
      markers,
      sizeof(markers)/
      sizeof(markers[0])
    );
}


static bool sfLooksProjectRelation(String relationSurface){

  relationSurface=
    normalizeInput(
      relationSurface
    );


  const char* const markers[]={

    "project",
    "prototype",
    "build",
    "invention"
  };


  return
    sfContainsAny(
      relationSurface,
      markers,
      sizeof(markers)/
      sizeof(markers[0])
    );
}


static bool sfLooksRelationshipNoun(String relationSurface){

  relationSurface=
    normalizeInput(
      relationSurface
    );


  const char* const relationshipNouns[]={

    "sister","sisters",
    "brother","brothers",
    "sibling","siblings",

    "mother","mom","mum",
    "father","dad","papa",
    "parent","parents",

    "cousin","cousins",
    "uncle","uncles",
    "aunt","aunts",

    "friend","friends",
    "best friend","best friends",

    "teacher","teachers",
    "mentor","mentors",

    "pet","pets"
  };


  return
    sfContainsAny(
      relationSurface,
      relationshipNouns,
      sizeof(relationshipNouns)/
      sizeof(relationshipNouns[0])
    );
}


static ElliSemanticRelationKind sfRelationForAttribute(
  String relationSurface,
  String value
){

  relationSurface=
    normalizeInput(
      relationSurface
    );

  value=
    normalizeInput(
      value
    );


  if(
    sfLooksEducationLevel(
      relationSurface+
      " "+
      value
    )
  ){

    return
      ELLI_SEM_REL_EDUCATION_LEVEL;
  }


  if(
    relationSurface=="age" ||
    sfLooksAge(value)
  ){

    return
      ELLI_SEM_REL_AGE;
  }


  if(
    sfLooksRelationshipNoun(
      relationSurface
    )
  ){

    return
      ELLI_SEM_REL_RELATIONSHIP;
  }


  if(
    relationSurface.indexOf("favorite")>=0 ||
    relationSurface.indexOf("favourite")>=0 ||
    relationSurface.indexOf("preference")>=0
  ){

    return
      ELLI_SEM_REL_PREFERENCE;
  }


  if(
    relationSurface.indexOf("language")>=0
  ){

    return
      ELLI_SEM_REL_LANGUAGE;
  }


  if(
    sfLooksProjectRelation(
      relationSurface
    )
  ){

    return
      ELLI_SEM_REL_PROJECT;
  }


  if(
    relationSurface.indexOf("job")>=0 ||
    relationSurface.indexOf("occupation")>=0 ||
    relationSurface.indexOf("profession")>=0 ||
    relationSurface.indexOf("role")>=0
  ){

    return
      ELLI_SEM_REL_OCCUPATION;
  }


  if(
    relationSurface.indexOf("city")>=0 ||
    relationSurface.indexOf("country")>=0 ||
    relationSurface.indexOf("location")>=0 ||
    relationSurface.indexOf("home")>=0 ||
    relationSurface.indexOf("address")>=0
  ){

    return
      ELLI_SEM_REL_RESIDENCE;
  }


  return
    ELLI_SEM_REL_ATTRIBUTE;
}


// =====================================================
// SUBJECT / CERTAINTY
// =====================================================

static ElliSemanticSubjectKind sfSubjectKind(
  const ElliNLUFrame& nlu,
  String q
){

  q=
    normalizeInput(
      q
    );


  if(
    nlu.firstPerson ||
    q.startsWith("my ") ||
    q.indexOf(" my ")>=0
  ){

    return
      ELLI_SEM_SUBJECT_USER;
  }


  if(
    nlu.secondPerson ||
    q.startsWith("your ")
  ){

    return
      ELLI_SEM_SUBJECT_ELLI;
  }


  if(
    q.startsWith("we ") ||
    q.startsWith("our ")
  ){

    return
      ELLI_SEM_SUBJECT_GROUP;
  }


  if(q.length()){

    return
      ELLI_SEM_SUBJECT_ENTITY;
  }


  return
    ELLI_SEM_SUBJECT_UNKNOWN;
}


static ElliSemanticCertainty sfCertainty(
  const ElliNLUFrame& nlu,
  String q
){

  q=
    normalizeInput(
      q
    );


  if(nlu.question){

    return
      ELLI_SEM_CERT_QUESTIONED;
  }


  if(
    q.indexOf(" if ")>=0 ||
    q.startsWith("if ") ||
    q.indexOf(" unless ")>=0
  ){

    return
      ELLI_SEM_CERT_CONDITIONAL;
  }


  if(
    q.indexOf("probably")>=0 ||
    q.indexOf("likely")>=0
  ){

    return
      ELLI_SEM_CERT_PROBABLE;
  }


  if(
    q.indexOf("maybe")>=0 ||
    q.indexOf("perhaps")>=0 ||
    sfBoundedToken(q,"might") ||
    sfBoundedToken(q,"could") ||
    sfBoundedToken(q,"may")
  ){

    return
      ELLI_SEM_CERT_POSSIBLE;
  }


  if(nlu.negated){

    return
      ELLI_SEM_CERT_NEGATED;
  }


  return
    ELLI_SEM_CERT_ASSERTED;
}


// =====================================================
// QUESTION MEANING
// =====================================================

static void sfAnalyzeQuestion(
  ElliSemanticFrame& f,
  String q
){

  // Personal relationship / attribute query:
  //
  //   who are my sisters
  //   what is my grade
  //   where is my school

  const char* const myMarkers[]={
    " my "
  };


  if(
    q.indexOf(" my ")>=0
  ){

    int myAt=
      q.indexOf(
        " my "
      );


    String tail=
      q.substring(
        myAt+4
      );


    tail.trim();


    // Remove trailing copular grammar in forms like:
    // "tell me who my sisters are"
    if(tail.endsWith(" are")){
      tail.remove(tail.length()-4);
    }
    else if(tail.endsWith(" is")){
      tail.remove(tail.length()-3);
    }


    // Quantity grammar:
    // "how many sisters do i have"
    int doI=
      tail.indexOf(
        " do i "
      );


    if(doI>=0){
      tail=
        tail.substring(
          0,
          doI
        );
    }


    tail.trim();


    if(tail.length()){

      f.relationSurface=
        tail;

      f.relationLabel=
        sfUpperRelationLabel(
          tail
        );

      f.relationKind=
        sfRelationForAttribute(
          tail,
          ""
        );


      if(
        sfLooksRelationshipNoun(
          tail
        )
      ){

        f.relationKind=
          ELLI_SEM_REL_RELATIONSHIP;
      }


      f.confidence=
        max(
          f.confidence,
          (uint8_t)88
        );
    }
  }


  // First-person semantic recall/reasoning forms.
  if(
    q.indexOf("grade am i")>=0 ||
    q.indexOf("class am i")>=0 ||
    q.indexOf("standard am i")>=0
  ){

    f.relationKind=
      ELLI_SEM_REL_EDUCATION_LEVEL;

    f.relationLabel=
      "EDUCATION_LEVEL";

    f.relationSurface=
      "education level";

    f.confidence=
      max(
        f.confidence,
        (uint8_t)94
      );
  }


  if(
    q.startsWith("where do i live") ||
    q.startsWith("where am i from") ||
    q.startsWith("where do i stay")
  ){

    f.relationKind=
      ELLI_SEM_REL_RESIDENCE;

    f.relationLabel=
      "RESIDENCE";

    f.relationSurface=
      "residence";

    f.confidence=
      max(
        f.confidence,
        (uint8_t)94
      );
  }


  if(
    q.startsWith("how old am i")
  ){

    f.relationKind=
      ELLI_SEM_REL_AGE;

    f.relationLabel=
      "AGE";

    f.relationSurface=
      "age";

    f.confidence=
      max(
        f.confidence,
        (uint8_t)96
      );
  }


  // Advice/capability questions.
  if(
    q.indexOf("should i ")>=0 ||
    q.indexOf("could i ")>=0 ||
    q.indexOf("can i ")>=0 ||
    q.indexOf("would i ")>=0 ||
    q.indexOf("should we ")>=0 ||
    q.indexOf("could we ")>=0 ||
    q.indexOf("can we ")>=0
  ){

    f.relationKind=
      ELLI_SEM_REL_CAPABILITY;

    f.relationLabel=
      "CAPABILITY_OR_ADVICE";

    f.relationSurface=
      "capability or advice";

    f.confidence=
      max(
        f.confidence,
        (uint8_t)86
      );
  }


  // Extract an open main verb after "i" where possible.
  int iAt=
    q.indexOf(
      " i "
    );


  if(iAt>=0){

    String afterI=
      q.substring(
        iAt+3
      );


    afterI.trim();


    int space=
      afterI.indexOf(
        ' '
      );


    String verb=

      space>=0

      ?

      afterI.substring(
        0,
        space
      )

      :

      afterI;


    if(
      verb!="am" &&
      verb!="was" &&
      verb!="were" &&
      verb!="is" &&
      verb!="are" &&
      verb!="do" &&
      verb!="did" &&
      verb!="have" &&
      verb!="can" &&
      verb!="could" &&
      verb!="should" &&
      verb!="would" &&
      verb!="will"
    ){

      f.mainVerb=
        verb;


      if(
        f.relationKind==
        ELLI_SEM_REL_UNKNOWN
      ){

        f.relationKind=
          ELLI_SEM_REL_CUSTOM;

        f.relationSurface=
          verb;

        f.relationLabel=
          sfUpperRelationLabel(
            verb
          );
      }
    }
  }
}


// =====================================================
// STATEMENT MEANING
// =====================================================

static bool sfAnalyzePossessiveAttribute(
  ElliSemanticFrame& f,
  String q
){

  // Open vocabulary:
  //
  //   my favorite game is minecraft
  //   my school is abc high school
  //   my project name is kira

  if(
    !q.startsWith("my ")
  ){

    return false;
  }


  const char* const copulas[]={
    " is ",
    " are ",
    " was ",
    " were ",
    " will be "
  };


  String found;
  int p=
    sfFindFirstOf(
      q,
      copulas,
      sizeof(copulas)/
      sizeof(copulas[0]),
      found
    );


  if(p<0){
    return false;
  }


  String relation=
    q.substring(
      3,
      p
    );


  String value=
    q.substring(
      p+
      found.length()
    );


  relation.trim();
  value.trim();


  if(
    !relation.length() ||
    !value.length()
  ){

    return false;
  }


  f.relationSurface=
    relation;

  f.relationLabel=
    sfUpperRelationLabel(
      relation
    );

  f.value=
    value;

  f.relationKind=
    sfRelationForAttribute(
      relation,
      value
    );

  f.confidence=
    max(
      f.confidence,
      (uint8_t)94
    );


  return true;
}


static bool sfAnalyzeReverseRelationship(
  ElliSemanticFrame& f,
  String q
){

  // Open-ish relationship grammar:
  //
  //   sai and jagruti are my sisters
  //   max is my dog
  //
  // The relation noun becomes relationSurface.
  // The left-hand side becomes value.

  const char* const patterns[]={
    " are my ",
    " is my ",
    " were my ",
    " was my "
  };


  String found;
  int p=
    sfFindFirstOf(
      q,
      patterns,
      sizeof(patterns)/
      sizeof(patterns[0]),
      found
    );


  if(p<=0){
    return false;
  }


  String lhs=
    q.substring(
      0,
      p
    );


  String relation=
    q.substring(
      p+
      found.length()
    );


  lhs.trim();
  relation.trim();


  if(
    !lhs.length() ||
    !relation.length()
  ){

    return false;
  }


  if(
    !sfLooksRelationshipNoun(
      relation
    )
  ){

    return false;
  }


  f.relationKind=
    ELLI_SEM_REL_RELATIONSHIP;

  f.relationSurface=
    relation;

  f.relationLabel=
    sfUpperRelationLabel(
      relation
    );

  f.value=
    lhs;

  f.pluralValue=
    lhs.indexOf(" and ")>=0 ||
    relation.endsWith("s");

  f.confidence=
    max(
      f.confidence,
      (uint8_t)97
    );


  return true;
}


static void sfAnalyzeFirstPersonStatement(
  ElliSemanticFrame& f,
  String q
){

  // ---------------------------------------------------
  // EDUCATION / LOCATION / MEMBERSHIP:
  //
  //   i am in 10th grade
  //   i was in 9th grade last year
  //   i am in robotics club
  //   i am in yavatmal
  // ---------------------------------------------------

  const char* const inPrefixes[]={

    "i am in ",
    "i was in ",
    "i will be in ",
    "i have been in "
  };


  for(
    size_t i=0;
    i<
    sizeof(inPrefixes)/
    sizeof(inPrefixes[0]);
    i++
  ){

    String prefix=
      String(
        inPrefixes[i]
      );


    if(
      q.startsWith(
        prefix
      )
    ){

      String value=
        sfAfterPrefix(
          q,
          prefix
        );


      if(f.timeText.length()){

        value=
          sfRemoveSuffixPhrase(
            value,
            f.timeText
          );
      }


      value.trim();


      f.value=
        value;


      if(
        sfLooksEducationLevel(
          value
        )
      ){

        f.relationKind=
          ELLI_SEM_REL_EDUCATION_LEVEL;

        f.relationLabel=
          "EDUCATION_LEVEL";

        f.relationSurface=
          "education level";
      }

      else if(
        sfLooksMembership(
          value
        )
      ){

        f.relationKind=
          ELLI_SEM_REL_MEMBERSHIP;

        f.relationLabel=
          "MEMBERSHIP";

        f.relationSurface=
          "membership";
      }

      else{

        f.relationKind=
          ELLI_SEM_REL_LOCATION;

        f.relationLabel=
          "LOCATION_CONTEXT";

        f.relationSurface=
          "location";
      }


      f.mainVerb=
        "be";


      f.confidence=
        max(
          f.confidence,
          (uint8_t)94
        );


      return;
    }
  }


  // ---------------------------------------------------
  // RESIDENCE
  // ---------------------------------------------------

  const char* const residencePrefixes[]={

    "i live in ",
    "i live at ",
    "i stay in ",
    "i stay at ",
    "i reside in ",
    "i resided in ",
    "i lived in "
  };


  for(
    size_t i=0;
    i<
    sizeof(residencePrefixes)/
    sizeof(residencePrefixes[0]);
    i++
  ){

    String p=
      String(
        residencePrefixes[i]
      );


    if(
      q.startsWith(p)
    ){

      f.relationKind=
        ELLI_SEM_REL_RESIDENCE;

      f.relationLabel=
        "RESIDENCE";

      f.relationSurface=
        "residence";

      f.value=
        sfAfterPrefix(
          q,
          p
        );

      f.locationText=
        f.value;

      f.mainVerb=
        "live";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)96
        );


      return;
    }
  }


  // ---------------------------------------------------
  // PREFERENCES
  // ---------------------------------------------------

  struct PreferenceVerb {
    const char* phrase;
    const char* relation;
  };


  const PreferenceVerb preferenceVerbs[]={

    {"i do not like ","DISLIKE"},
    {"i do not enjoy ","DISLIKE"},
    {"i dislike ","DISLIKE"},
    {"i hate ","DISLIKE"},
    {"i cannot stand ","DISLIKE"},
    {"i can not stand ","DISLIKE"},

    {"i like ","LIKE"},
    {"i love ","LIKE"},
    {"i enjoy ","LIKE"},
    {"i prefer ","PREFER"}
  };


  for(
    size_t i=0;
    i<
    sizeof(preferenceVerbs)/
    sizeof(preferenceVerbs[0]);
    i++
  ){

    String phrase=
      String(
        preferenceVerbs[i].phrase
      );


    if(
      q.startsWith(
        phrase
      )
    ){

      f.relationKind=
        ELLI_SEM_REL_PREFERENCE;

      f.relationLabel=
        preferenceVerbs[i].relation;

      f.relationSurface=
        preferenceVerbs[i].relation;

      f.value=
        sfAfterPrefix(
          q,
          phrase
        );

      f.mainVerb=
        f.relationLabel;

      f.confidence=
        max(
          f.confidence,
          (uint8_t)96
        );


      return;
    }
  }


  // ---------------------------------------------------
  // POSSESSION
  // ---------------------------------------------------

  const char* const possessionPrefixes[]={

    "i have ",
    "i had ",
    "i will have "
  };


  for(
    size_t i=0;
    i<
    sizeof(possessionPrefixes)/
    sizeof(possessionPrefixes[0]);
    i++
  ){

    String p=
      String(
        possessionPrefixes[i]
      );


    if(
      q.startsWith(p)
    ){

      f.relationKind=
        ELLI_SEM_REL_POSSESSION;

      f.relationLabel=
        "HAS";

      f.relationSurface=
        "has";

      f.value=
        sfAfterPrefix(
          q,
          p
        );

      f.mainVerb=
        "have";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)91
        );


      return;
    }
  }


  // ---------------------------------------------------
  // EDUCATION ACTIVITY
  // ---------------------------------------------------

  const char* const studyPrefixes[]={

    "i study ",
    "i am studying ",
    "i was studying ",
    "i have studied ",
    "i have been studying ",
    "i learn ",
    "i am learning ",
    "i was learning "
  };


  for(
    size_t i=0;
    i<
    sizeof(studyPrefixes)/
    sizeof(studyPrefixes[0]);
    i++
  ){

    String p=
      String(
        studyPrefixes[i]
      );


    if(
      q.startsWith(p)
    ){

      f.relationKind=
        ELLI_SEM_REL_EDUCATION_ACTIVITY;

      f.relationLabel=
        "STUDY";

      f.relationSurface=
        "study";

      f.value=
        sfAfterPrefix(
          q,
          p
        );

      f.mainVerb=
        "study";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)91
        );


      return;
    }
  }


  // ---------------------------------------------------
  // LANGUAGE
  // ---------------------------------------------------

  const char* const speakPrefixes[]={

    "i speak ",
    "i can speak ",
    "i know how to speak "
  };


  for(
    size_t i=0;
    i<
    sizeof(speakPrefixes)/
    sizeof(speakPrefixes[0]);
    i++
  ){

    String p=
      String(
        speakPrefixes[i]
      );


    if(
      q.startsWith(p)
    ){

      f.relationKind=
        ELLI_SEM_REL_LANGUAGE;

      f.relationLabel=
        "LANGUAGE";

      f.relationSurface=
        "language";

      f.value=
        sfAfterPrefix(
          q,
          p
        );

      f.mainVerb=
        "speak";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)92
        );


      return;
    }
  }


  // ---------------------------------------------------
  // GOAL / INTENTION
  // ---------------------------------------------------

  const char* const goalPrefixes[]={

    "i want to ",
    "i want ",
    "i hope to ",
    "i aim to ",
    "i plan to ",
    "i intend to ",
    "my goal is "
  };


  for(
    size_t i=0;
    i<
    sizeof(goalPrefixes)/
    sizeof(goalPrefixes[0]);
    i++
  ){

    String p=
      String(
        goalPrefixes[i]
      );


    if(
      q.startsWith(p)
    ){

      f.relationKind=

        p.indexOf("plan")>=0 ||
        p.indexOf("intend")>=0

        ?

        ELLI_SEM_REL_INTENTION

        :

        ELLI_SEM_REL_GOAL;


      f.relationLabel=

        f.relationKind==
        ELLI_SEM_REL_INTENTION

        ?

        "INTENTION"

        :

        "GOAL";


      f.relationSurface=
        f.relationLabel;

      f.value=
        sfAfterPrefix(
          q,
          p
        );

      f.mainVerb=
        "want";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)90
        );


      return;
    }
  }


  // ---------------------------------------------------
  // CAPABILITY
  // ---------------------------------------------------

  const char* const capabilityPrefixes[]={

    "i can ",
    "i could ",
    "i am able to ",
    "i was able to "
  };


  for(
    size_t i=0;
    i<
    sizeof(capabilityPrefixes)/
    sizeof(capabilityPrefixes[0]);
    i++
  ){

    String p=
      String(
        capabilityPrefixes[i]
      );


    if(
      q.startsWith(p)
    ){

      f.relationKind=
        ELLI_SEM_REL_CAPABILITY;

      f.relationLabel=
        "CAPABILITY";

      f.relationSurface=
        "capability";

      f.value=
        sfAfterPrefix(
          q,
          p
        );

      f.mainVerb=
        "can";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)89
        );


      return;
    }
  }


  // ---------------------------------------------------
  // GENERAL "I AM / I WAS / I WILL BE"
  // ---------------------------------------------------

  const char* const bePrefixes[]={

    "i am ",
    "i was ",
    "i will be ",
    "i have been "
  };


  for(
    size_t i=0;
    i<
    sizeof(bePrefixes)/
    sizeof(bePrefixes[0]);
    i++
  ){

    String p=
      String(
        bePrefixes[i]
      );


    if(
      q.startsWith(p)
    ){

      String value=
        sfAfterPrefix(
          q,
          p
        );


      if(f.timeText.length()){

        value=
          sfRemoveSuffixPhrase(
            value,
            f.timeText
          );
      }


      f.value=
        value;


      if(sfLooksAge(value)){

        f.relationKind=
          ELLI_SEM_REL_AGE;

        f.relationLabel=
          "AGE";

        f.relationSurface=
          "age";
      }

      else{

        f.relationKind=
          ELLI_SEM_REL_STATE;

        f.relationLabel=
          "STATE";

        f.relationSurface=
          "state";
      }


      f.mainVerb=
        "be";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)88
        );


      return;
    }
  }


  // ---------------------------------------------------
  // OPEN-VOCABULARY FALLBACK
  // ---------------------------------------------------
  //
  // Rather than forcing an unknown personal statement into
  // UNKNOWN, use the first content verb as a CUSTOM relation.
  //
  // This is the scalability escape hatch.
  // ---------------------------------------------------

  if(
    q.startsWith("i ")
  ){

    String rest=
      q.substring(2);

    rest.trim();


    int space=
      rest.indexOf(
        ' '
      );


    String verb=

      space>=0

      ?

      rest.substring(
        0,
        space
      )

      :

      rest;


    String value=

      space>=0

      ?

      rest.substring(
        space+1
      )

      :

      "";


    verb.trim();
    value.trim();


    if(verb.length()){

      f.mainVerb=
        verb;

      f.relationKind=
        ELLI_SEM_REL_CUSTOM;

      f.relationSurface=
        verb;

      f.relationLabel=
        sfUpperRelationLabel(
          verb
        );

      f.value=
        value;

      f.confidence=
        max(
          f.confidence,
          (uint8_t)70
        );
    }
  }
}


// =====================================================
// PUBLIC BUILDER
// =====================================================

ElliSemanticFrame elliBuildSemanticFrame(
  String input
){

  ElliSemanticFrame f;


  f.raw=
    input;


  f.normalized=
    normalizeInput(
      input
    );


  ElliNLUFrame nlu=
    elliAnalyzeNLU(
      f.normalized
    );


  f.tense=
    nlu.tense;

  f.aspect=
    nlu.aspect;

  f.question=
    nlu.question;

  f.personal=
    nlu.personal;

  f.negated=
    nlu.negated;

  f.modal=
    nlu.modal;

  f.compound=
    nlu.compound;

  f.subjectKind=
    sfSubjectKind(
      nlu,
      f.normalized
    );


  if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_USER
  ){

    f.subject="USER";
  }

  else if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_ELLI
  ){

    f.subject="ELLI";
  }

  else if(
    f.subjectKind==
    ELLI_SEM_SUBJECT_GROUP
  ){

    f.subject="GROUP";
  }

  else{

    f.subject="";
  }


  f.certainty=
    sfCertainty(
      nlu,
      f.normalized
    );


  f.timeText=
    sfExtractTimeText(
      f.normalized
    );


  f.queryTarget=
    sfQueryTarget(
      f.normalized,
      f.questionWord
    );


  f.confidence=
    nlu.confidence;


  if(f.question){

    sfAnalyzeQuestion(
      f,
      f.normalized
    );
  }

  else{

    bool handled=

      sfAnalyzePossessiveAttribute(
        f,
        f.normalized
      );


    if(!handled){

      handled=
        sfAnalyzeReverseRelationship(
          f,
          f.normalized
        );
    }


    if(
      !handled &&
      f.subjectKind==
      ELLI_SEM_SUBJECT_USER
    ){

      sfAnalyzeFirstPersonStatement(
        f,
        f.normalized
      );
    }
  }


  // If we still do not have a stable relation, use the NLU kind as
  // a low-confidence semantic classification rather than inventing
  // a specific meaning.
  if(
    f.relationKind==
    ELLI_SEM_REL_UNKNOWN
  ){

    if(f.question){

      f.relationKind=
        ELLI_SEM_REL_CUSTOM;

      f.relationLabel=
        "OPEN_QUESTION";

      f.relationSurface=
        "open question";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)58
        );
    }

    else if(nlu.statement){

      f.relationKind=
        ELLI_SEM_REL_CUSTOM;

      f.relationLabel=
        "OPEN_STATEMENT";

      f.relationSurface=
        "open statement";

      f.confidence=
        max(
          f.confidence,
          (uint8_t)55
        );
    }
  }


  f.value=
    sfTrimPunctuation(
      f.value
    );


  f.valid=

    f.normalized.length()

    &&

    (
      f.relationKind!=
      ELLI_SEM_REL_UNKNOWN

      ||

      f.queryTarget!=
      ELLI_SEM_QUERY_NONE
    );


  return f;
}


// =====================================================
// HUMAN-READABLE NAMES
// =====================================================

const char* elliSemanticSubjectName(
  ElliSemanticSubjectKind kind
){

  switch(kind){

    case ELLI_SEM_SUBJECT_USER:
      return "USER";

    case ELLI_SEM_SUBJECT_ELLI:
      return "ELLI";

    case ELLI_SEM_SUBJECT_GROUP:
      return "GROUP";

    case ELLI_SEM_SUBJECT_ENTITY:
      return "ENTITY";

    default:
      return "UNKNOWN";
  }
}


const char* elliSemanticRelationName(
  ElliSemanticRelationKind kind
){

  switch(kind){

    case ELLI_SEM_REL_ATTRIBUTE:
      return "ATTRIBUTE";

    case ELLI_SEM_REL_IDENTITY:
      return "IDENTITY";

    case ELLI_SEM_REL_STATE:
      return "STATE";

    case ELLI_SEM_REL_EDUCATION_LEVEL:
      return "EDUCATION_LEVEL";

    case ELLI_SEM_REL_EDUCATION_ACTIVITY:
      return "EDUCATION_ACTIVITY";

    case ELLI_SEM_REL_AGE:
      return "AGE";

    case ELLI_SEM_REL_RELATIONSHIP:
      return "RELATIONSHIP";

    case ELLI_SEM_REL_PREFERENCE:
      return "PREFERENCE";

    case ELLI_SEM_REL_POSSESSION:
      return "POSSESSION";

    case ELLI_SEM_REL_RESIDENCE:
      return "RESIDENCE";

    case ELLI_SEM_REL_LOCATION:
      return "LOCATION";

    case ELLI_SEM_REL_MEMBERSHIP:
      return "MEMBERSHIP";

    case ELLI_SEM_REL_OCCUPATION:
      return "OCCUPATION";

    case ELLI_SEM_REL_LANGUAGE:
      return "LANGUAGE";

    case ELLI_SEM_REL_PROJECT:
      return "PROJECT";

    case ELLI_SEM_REL_ACTIVITY:
      return "ACTIVITY";

    case ELLI_SEM_REL_CAPABILITY:
      return "CAPABILITY";

    case ELLI_SEM_REL_GOAL:
      return "GOAL";

    case ELLI_SEM_REL_INTENTION:
      return "INTENTION";

    case ELLI_SEM_REL_QUANTITY:
      return "QUANTITY";

    case ELLI_SEM_REL_TIME:
      return "TIME";

    case ELLI_SEM_REL_CUSTOM:
      return "CUSTOM";

    default:
      return "UNKNOWN";
  }
}


const char* elliSemanticCertaintyName(
  ElliSemanticCertainty certainty
){

  switch(certainty){

    case ELLI_SEM_CERT_ASSERTED:
      return "ASSERTED";

    case ELLI_SEM_CERT_NEGATED:
      return "NEGATED";

    case ELLI_SEM_CERT_POSSIBLE:
      return "POSSIBLE";

    case ELLI_SEM_CERT_PROBABLE:
      return "PROBABLE";

    case ELLI_SEM_CERT_CONDITIONAL:
      return "CONDITIONAL";

    case ELLI_SEM_CERT_QUESTIONED:
      return "QUESTIONED";

    default:
      return "UNKNOWN";
  }
}


const char* elliSemanticQueryTargetName(
  ElliSemanticQueryTarget target
){

  switch(target){

    case ELLI_SEM_QUERY_VALUE:
      return "VALUE";

    case ELLI_SEM_QUERY_PERSON:
      return "PERSON";

    case ELLI_SEM_QUERY_PLACE:
      return "PLACE";

    case ELLI_SEM_QUERY_TIME:
      return "TIME";

    case ELLI_SEM_QUERY_REASON:
      return "REASON";

    case ELLI_SEM_QUERY_METHOD:
      return "METHOD";

    case ELLI_SEM_QUERY_CHOICE:
      return "CHOICE";

    case ELLI_SEM_QUERY_QUANTITY:
      return "QUANTITY";

    case ELLI_SEM_QUERY_BOOLEAN:
      return "BOOLEAN";

    default:
      return "NONE";
  }
}


// =====================================================
// DEBUG
// =====================================================

void printElliSemanticFrame(
  String input
){

  ElliSemanticFrame f=
    elliBuildSemanticFrame(
      input
    );


  Serial.println();

  Serial.println(
    "========== ELLI SEMANTIC FRAME V2 =========="
  );


  Serial.print(
    "Input          : "
  );

  Serial.println(
    f.normalized
  );


  Serial.print(
    "Valid          : "
  );

  Serial.println(
    f.valid
    ?
    "YES"
    :
    "NO"
  );


  Serial.print(
    "Subject kind   : "
  );

  Serial.println(
    elliSemanticSubjectName(
      f.subjectKind
    )
  );


  Serial.print(
    "Subject        : "
  );

  Serial.println(
    f.subject
  );


  Serial.print(
    "Relation kind  : "
  );

  Serial.println(
    elliSemanticRelationName(
      f.relationKind
    )
  );


  Serial.print(
    "Relation label : "
  );

  Serial.println(
    f.relationLabel
  );


  Serial.print(
    "Relation text  : "
  );

  Serial.println(
    f.relationSurface
  );


  Serial.print(
    "Value          : "
  );

  Serial.println(
    f.value
  );


  Serial.print(
    "Object         : "
  );

  Serial.println(
    f.object
  );


  Serial.print(
    "Time           : "
  );

  Serial.println(
    f.timeText
  );


  Serial.print(
    "Location       : "
  );

  Serial.println(
    f.locationText
  );


  Serial.print(
    "Query target   : "
  );

  Serial.println(
    elliSemanticQueryTargetName(
      f.queryTarget
    )
  );


  Serial.print(
    "Tense          : "
  );

  Serial.println(
    elliTenseName(
      f.tense
    )
  );


  Serial.print(
    "Aspect         : "
  );

  Serial.println(
    elliAspectName(
      f.aspect
    )
  );


  Serial.print(
    "Certainty      : "
  );

  Serial.println(
    elliSemanticCertaintyName(
      f.certainty
    )
  );


  Serial.print(
    "Negated        : "
  );

  Serial.println(
    f.negated
    ?
    "YES"
    :
    "NO"
  );


  Serial.print(
    "Modal          : "
  );

  Serial.println(
    f.modal
    ?
    "YES"
    :
    "NO"
  );


  Serial.print(
    "Compound       : "
  );

  Serial.println(
    f.compound
    ?
    "YES"
    :
    "NO"
  );


  Serial.print(
    "Confidence     : "
  );

  Serial.println(
    f.confidence
  );


  Serial.println(
    "============================================="
  );
}
