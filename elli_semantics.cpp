#include <Arduino.h>
#include <WiFi.h>

#include "elli_calendar.h"
#include "elli_semantics.h"
#include "elli_intent.h"


// =====================================================
//          FUNCTIONS PROVIDED BY kira_brain.ino
// =====================================================

String normalizeInput(String s);

void elliSay(const String& s);

bool webRandomFact(
  const String& topic,
  String& answer,
  String& source,
  int& confidence
);

String currentTimeString();

String currentDateString();

String urlEncode(const String& s);

String extractJsonString(
  const String& j,
  const String& key
);

String cleanWebAnswer(
  String input,
  int maxChars,
  int maxSentences
);

bool httpsGet(
  const String& url,
  String& body,
  int& code
);


// =====================================================
//                    BASIC UTILITIES
// =====================================================

bool charIsWord(char c){

  return
    isalnum((unsigned char)c) ||
    c=='-' ||
    c=='_';
}


String semanticCleanToken(String s){

  s.toLowerCase();

  String out="";

  for(size_t i=0;i<s.length();i++){

    char c=s[i];

    if(
      isalnum(
        (unsigned char)c
      )
    ){
      out+=c;
    }
  }

  return out;
}


int semanticWordCount(String q){

  q=normalizeInput(q);

  q.trim();

  if(!q.length()){
    return 0;
  }

  int count=0;

  bool inWord=false;

  for(size_t i=0;i<=q.length();i++){

    bool word=
      (i<q.length()) &&
      isalnum(
        (unsigned char)q[i]
      );

    if(
      word &&
      !inWord
    ){
      count++;
      inWord=true;
    }

    if(!word){
      inWord=false;
    }
  }

  return count;
}


// =====================================================
//                    STOP WORDS
// =====================================================

bool semanticStopWord(String w){

  const char* stop[]={

    "a",
    "an",
    "the",

    "is",
    "are",
    "was",
    "were",
    "be",
    "been",
    "being",

    "what",
    "which",
    "who",
    "whom",
    "whose",
    "where",
    "when",
    "why",
    "how",

    "do",
    "does",
    "did",

    "can",
    "could",
    "would",
    "should",
    "will",

    "tell",
    "give",
    "show",

    "me",
    "you",
    "your",
    "my",
    "our",
    "their",

    "of",
    "in",
    "on",
    "at",
    "to",
    "from",
    "for",
    "with",
    "about",
    "and",
    "or",
    "by",

    "this",
    "that",
    "these",
    "those",
    "there",

    "any",
    "some",
    "please"
  };

  for(
    size_t i=0;
    i<sizeof(stop)/sizeof(stop[0]);
    i++
  ){

    if(w==stop[i]){
      return true;
    }
  }

  return false;
}


// =====================================================
//                      STEMMING
// =====================================================
//
// Used ONLY for comparing source evidence.
//
// It does NOT rewrite the user's sentence.
// =====================================================

String semanticStem(String w){

  w=semanticCleanToken(w);

  if(w.length()<4){
    return w;
  }

  if(
    w.endsWith("ies") &&
    w.length()>5
  ){

    w.remove(
      w.length()-3
    );

    w+="y";
  }

  else if(
    w.endsWith("ing") &&
    w.length()>6
  ){

    w.remove(
      w.length()-3
    );
  }

  else if(
    w.endsWith("ed") &&
    w.length()>5
  ){

    w.remove(
      w.length()-2
    );
  }

  else if(
    w.endsWith("es") &&
    w.length()>5
  ){

    w.remove(
      w.length()-2
    );
  }

  else if(
    w.endsWith("s") &&
    w.length()>4
  ){

    w.remove(
      w.length()-1
    );
  }

  return w;
}


bool semanticTokenRelated(
  String a,
  String b
){

  a=semanticStem(a);
  b=semanticStem(b);

  if(
    !a.length() ||
    !b.length()
  ){
    return false;
  }

  if(a==b){
    return true;
  }

  int minLen=
    min(
      (int)a.length(),
      (int)b.length()
    );

  if(minLen>=5){

    int same=0;

    while(
      same<minLen &&
      a[same]==b[same]
    ){
      same++;
    }

    if(same>=5){
      return true;
    }
  }

  return false;
}


// =====================================================
//               TOKEN EXTRACTION
// =====================================================

int collectSemanticTokens(
  String text,
  String out[],
  int maxCount
){

  text=normalizeInput(text);

  int count=0;

  String current="";

  for(size_t i=0;i<=text.length();i++){

    char c=
      (i<text.length())
      ? text[i]
      : ' ';

    if(
      isalnum(
        (unsigned char)c
      )
    ){

      current+=c;
    }

    else if(current.length()){

      String token=
        semanticCleanToken(current);

      if(
        token.length() &&
        !semanticStopWord(token)
      ){

        bool duplicate=false;

        for(int j=0;j<count;j++){

          if(
            semanticTokenRelated(
              out[j],
              token
            )
          ){

            duplicate=true;

            break;
          }
        }

        if(
          !duplicate &&
          count<maxCount
        ){

          out[count++]=token;
        }
      }

      current="";
    }
  }

  return count;
}


// =====================================================
//                    BASIC PHRASES
// =====================================================

bool hasPhrase(
  String text,
  const char* phrase
){

  text=normalizeInput(text);

  return
    text.indexOf(phrase)>=0;
}


bool looksQuestionLike(String q){

  q=normalizeInput(q);

  const char* starters[]={

    "what ",
    "which ",
    "who ",
    "where ",
    "when ",
    "why ",
    "how ",

    "is ",
    "are ",

    "can ",
    "could ",

    "do ",
    "does ",
    "did ",

    "define ",
    "explain ",

    "tell me ",
    "give me ",
    "show me ",

    "compare "
  };

  for(
    size_t i=0;
    i<sizeof(starters)/sizeof(starters[0]);
    i++
  ){

    if(
      q.startsWith(
        starters[i]
      )
    ){

      return true;
    }
  }

  return false;
}


// =====================================================
//                  FACT REQUESTS
// =====================================================

bool isFactRequest(String q){

  q=normalizeInput(q);

  bool hasFact=

    q.indexOf(" fact")>=0 ||

    q.startsWith(
      "fact"
    ) ||

    q.indexOf(
      "facts"
    )>=0;

  if(!hasFact){
    return false;
  }

  bool request=

    q.indexOf("tell")>=0 ||

    q.indexOf("give")>=0 ||

    q.indexOf("show")>=0 ||

    q.indexOf("random")>=0 ||

    q.indexOf("fun fact")>=0 ||

    q.indexOf("another fact")>=0 ||

    q.indexOf("one more fact")>=0 ||

    q.indexOf("any fact")>=0 ||

    q.indexOf("some fact")>=0;

  return request;
}


String extractFactTopic(String q){

  q=normalizeInput(q);

  int about=
    q.indexOf(
      " about "
    );

  if(about>=0){

    String topic=
      q.substring(
        about+7
      );

    topic.replace(
      "facts",
      ""
    );

    topic.replace(
      "fact",
      ""
    );

    topic.trim();

    return topic;
  }

  String tokens[16];

  int count=
    collectSemanticTokens(
      q,
      tokens,
      16
    );

  String topic="";

  const char* requestWords[]={

    "fact",
    "facts",

    "random",
    "fun",

    "another",

    "one",
    "more",

    "tell",
    "give",
    "show",

    "any",
    "some"
  };

  for(int i=0;i<count;i++){

    bool skip=false;

    for(
      size_t j=0;
      j<
      sizeof(requestWords)/
      sizeof(requestWords[0]);
      j++
    ){

      if(
        tokens[i]
        ==
        requestWords[j]
      ){

        skip=true;

        break;
      }
    }

    if(skip){
      continue;
    }

    if(topic.length()){
      topic+=" ";
    }

    topic+=tokens[i];
  }

  topic.trim();

  return topic;
}


// =====================================================
//              COMPARISON OPERATORS
// =====================================================

bool tokenIsMaximumOperator(String w){

  return

    w=="most" ||

    w=="maximum" ||

    w=="max" ||

    w=="highest" ||

    w=="largest" ||

    w=="biggest" ||

    w=="greatest";
}


bool tokenIsMinimumOperator(String w){

  return

    w=="least" ||

    w=="minimum" ||

    w=="min" ||

    w=="lowest" ||

    w=="smallest";
}


bool tokenLooksSuperlative(String w){

  w=semanticCleanToken(w);

  if(w.length()<5){
    return false;
  }

  return
    w.endsWith("est");
}


// =====================================================
//                  CRITERION LANGUAGE
// =====================================================

bool hasCriterionLanguage(String q){

  q=normalizeInput(q);

  const char* markers[]={

    "according to ",

    "based on ",

    "ranked by ",

    "measured by ",

    "in terms of ",

    "using ",

    "for research",

    "for engineering",

    "for science",

    "for speed",

    "for price",

    "for performance"
  };

  for(
    size_t i=0;
    i<sizeof(markers)/sizeof(markers[0]);
    i++
  ){

    if(
      q.indexOf(
        markers[i]
      )>=0
    ){

      return true;
    }
  }

  return false;
}


// =====================================================
//                  SEMANTIC SUBJECT
// =====================================================

String semanticSubject(String q){

  q=normalizeInput(q);

  const char* prefixes[]={

    "what is meant by ",

    "what are meant by ",

    "what is ",

    "what are ",

    "who is ",

    "who was ",

    "define ",

    "explain ",

    "tell me about ",

    "give me information about ",

    "meaning of "
  };

  for(
    size_t i=0;
    i<sizeof(prefixes)/sizeof(prefixes[0]);
    i++
  ){

    String p=
      prefixes[i];

    if(q.startsWith(p)){

      q.remove(
        0,
        p.length()
      );

      break;
    }
  }

  if(q.startsWith("a ")){

    q.remove(
      0,
      2
    );
  }

  else if(q.startsWith("an ")){

    q.remove(
      0,
      3
    );
  }

  else if(q.startsWith("the ")){

    q.remove(
      0,
      4
    );
  }

  q.trim();

  return q;
}


// =====================================================
//                SEMANTIC ANALYSIS
// =====================================================

SemanticFrame analyzeSemanticQuery(
  const String& input
){

  String q=
    normalizeInput(input);

  SemanticFrame f;

  f.kind=
    SEM_UNKNOWN;

  f.op=
    SEM_OP_NONE;

  f.question=
    looksQuestionLike(q);

  f.subject=
    semanticSubject(q);

  f.topic="";

  f.subjective=false;

  f.hasCriterion=
    hasCriterionLanguage(q);

  f.confidence=45;


  // ---------------------------------------------------
  // FACT REQUEST
  // ---------------------------------------------------

  if(isFactRequest(q)){

    f.kind=
      SEM_FACT_REQUEST;

    f.topic=
      extractFactTopic(q);

    f.confidence=96;

    return f;
  }


  // ---------------------------------------------------
  // COMPARISON
  // ---------------------------------------------------

  bool comparison=

    q.indexOf(
      "compare "
    )>=0 ||

    q.indexOf(
      "difference between "
    )>=0 ||

    q.indexOf(
      " versus "
    )>=0 ||

    q.indexOf(
      " vs "
    )>=0;

  if(comparison){

    f.kind=
      SEM_COMPARISON;

    f.op=
      SEM_OP_COMPARE;

    f.confidence=92;

    return f;
  }


  // ---------------------------------------------------
  // RANKING / SUPERLATIVE
  // ---------------------------------------------------

  String words[24];

  int wc=
    collectSemanticTokens(
      q,
      words,
      24
    );

  bool ranking=false;

  for(int i=0;i<wc;i++){

    String w=
      words[i];

    if(
      w=="best" ||
      w=="top"
    ){

      f.op=
        SEM_OP_BEST;

      f.subjective=true;

      ranking=true;

      break;
    }

    if(w=="worst"){

      f.op=
        SEM_OP_WORST;

      f.subjective=true;

      ranking=true;

      break;
    }

    if(
      tokenIsMaximumOperator(w)
    ){

      f.op=
        SEM_OP_MAX;

      ranking=true;

      break;
    }

    if(
      tokenIsMinimumOperator(w)
    ){

      f.op=
        SEM_OP_MIN;

      ranking=true;

      break;
    }

    if(
      tokenLooksSuperlative(w)
    ){

      f.op=
        SEM_OP_EXTREME;

      ranking=true;

      break;
    }
  }

  if(ranking){

    f.kind=
      SEM_RANKING;

    f.confidence=88;

    return f;
  }


  // ---------------------------------------------------
  // DEFINITION
  // ---------------------------------------------------

  bool definition=

    q.startsWith(
      "what is "
    ) ||

    q.startsWith(
      "what are "
    ) ||

    q.startsWith(
      "what is meant by "
    ) ||

    q.startsWith(
      "define "
    ) ||

    q.startsWith(
      "meaning of "
    ) ||

    q.startsWith(
      "explain "
    ) ||

    q.startsWith(
      "tell me about "
    );

  if(definition){

    f.kind=
      SEM_DEFINITION;

    f.confidence=94;

    return f;
  }


  // ---------------------------------------------------
  // GENERIC LOOKUP
  // ---------------------------------------------------

  if(f.question){

    f.kind=
      SEM_LOOKUP;

    f.confidence=82;
  }

  return f;
}


// =====================================================
//                    DEBUG NAMES
// =====================================================

const char* semanticKindName(int kind){

  switch(kind){

    case SEM_FACT_REQUEST:
      return "FACT REQUEST";

    case SEM_DEFINITION:
      return "DEFINITION";

    case SEM_LOOKUP:
      return "LOOKUP";

    case SEM_COMPARISON:
      return "COMPARISON";

    case SEM_RANKING:
      return "RANKING / SUPERLATIVE";

    default:
      return "UNCLASSIFIED";
  }
}


const char* semanticOpName(int op){

  switch(op){

    case SEM_OP_MAX:
      return "MAX";

    case SEM_OP_MIN:
      return "MIN";

    case SEM_OP_BEST:
      return "BEST";

    case SEM_OP_WORST:
      return "WORST";

    case SEM_OP_COMPARE:
      return "COMPARE";

    case SEM_OP_EXTREME:
      return "SUPERLATIVE";

    default:
      return "NONE";
  }
}


void printSemanticFrame(
  const SemanticFrame& f
){

  Serial.println(
    "[SEMANTIC FRAME]"
  );

  Serial.print(
    "  kind       : "
  );

  Serial.println(
    semanticKindName(f.kind)
  );

  Serial.print(
    "  operator   : "
  );

  Serial.println(
    semanticOpName(f.op)
  );

  Serial.print(
    "  subject    : "
  );

  Serial.println(
    f.subject
  );

  Serial.print(
    "  confidence : "
  );

  Serial.println(
    f.confidence
  );
}


// =====================================================
//              CURRENT TIME / DATE UTILITY
// =====================================================

static String semanticTimeOnly(){

  String full=
    currentTimeString();

  int sep=
    full.indexOf(
      " - "
    );

  if(sep>0){

    full=
      full.substring(
        0,
        sep
      );
  }

  full.trim();

  return full;
}


static bool handleLocalTemporalUtility(
  const String& input
){

  TemporalRequestKind kind=
    detectLocalTemporalRequest(
      input
    );

  if(
    kind==
    TEMPORAL_NONE
  ){

    return false;
  }


  // ---------------------------------------------------
  // DATE + TIME
  // ---------------------------------------------------

  if(
    kind==
    TEMPORAL_DATE_TIME
  ){

    Serial.println(
      "[LOCAL UTILITY] DATE + TIME"
    );

    elliSay(

      "Today is "+

      currentDateString()+

      ", and the time is "+

      semanticTimeOnly()+

      "."
    );
  }


  // ---------------------------------------------------
  // DATE ONLY
  // ---------------------------------------------------

  else if(
    kind==
    TEMPORAL_DATE
  ){

    Serial.println(
      "[LOCAL UTILITY] DATE"
    );

    elliSay(

      "Today is "+

      currentDateString()+

      "."
    );
  }


  // ---------------------------------------------------
  // TIME ONLY
  // ---------------------------------------------------

  else{

    Serial.println(
      "[LOCAL UTILITY] TIME"
    );

    elliSay(

      "It is "+

      semanticTimeOnly()+

      "."
    );
  }

  return true;
}


// =====================================================
//                 GENERIC CALENDAR ENGINE
// =====================================================
//
// This is the important new layer.
//
// No dates are hard-coded.
//
// It understands:
//
//   29 august
//   august 29
//   29th august
//   29 august 2027
//   29/08/2027
//   today
//   tomorrow
//   yesterday
//
// It can:
//
//   calculate weekday locally
//
// and:
//
//   fetch historical significance / observances from web.
//
// =====================================================

struct ParsedCalendarDate {

  bool valid;

  int day;

  int month;

  int year;

  bool explicitYear;

  bool relative;
};


// =====================================================
//                   MONTH HANDLING
// =====================================================

static String monthNameFull(int month){

  const char* names[]={

    "January",

    "February",

    "March",

    "April",

    "May",

    "June",

    "July",

    "August",

    "September",

    "October",

    "November",

    "December"
  };

  if(
    month<1 ||
    month>12
  ){

    return "";
  }

  return
    String(
      names[month-1]
    );
}


static int monthFromToken(String token){

  token=
    semanticCleanToken(
      token
    );

  if(token.length()>=3){

    String p=
      token.substring(
        0,
        3
      );

    const char* keys[]={

      "jan",
      "feb",
      "mar",
      "apr",
      "may",
      "jun",
      "jul",
      "aug",
      "sep",
      "oct",
      "nov",
      "dec"
    };

    for(int i=0;i<12;i++){

      if(p==keys[i]){

        return i+1;
      }
    }
  }


  // Numeric month

  bool numeric=
    token.length()>0;

  for(size_t i=0;i<token.length();i++){

    if(
      !isdigit(
        (unsigned char)token[i]
      )
    ){

      numeric=false;

      break;
    }
  }

  if(numeric){

    int n=
      token.toInt();

    if(
      n>=1 &&
      n<=12
    ){

      return n;
    }
  }

  return 0;
}


// =====================================================
//                   NUMBER PARSER
// =====================================================

static int positiveNumberFromToken(
  String token
){

  token.toLowerCase();

  token.trim();

  // Ordinal suffixes:
  // 1st, 2nd, 3rd, 4th...

  if(
    token.endsWith("st") ||
    token.endsWith("nd") ||
    token.endsWith("rd") ||
    token.endsWith("th")
  ){

    token.remove(
      token.length()-2
    );
  }

  if(!token.length()){

    return -1;
  }

  for(size_t i=0;i<token.length();i++){

    if(
      !isdigit(
        (unsigned char)token[i]
      )
    ){

      return -1;
    }
  }

  return
    token.toInt();
}


// =====================================================
//                 DATE VALIDATION
// =====================================================

static bool validCalendarDate(
  int day,
  int month,
  int year
){

  if(
    year<1900 ||
    year>2200
  ){

    return false;
  }

  if(
    month<1 ||
    month>12
  ){

    return false;
  }

  if(
    day<1 ||
    day>31
  ){

    return false;
  }

  struct tm t={};

  t.tm_year=
    year-1900;

  t.tm_mon=
    month-1;

  t.tm_mday=
    day;

  t.tm_hour=
    12;

  t.tm_isdst=
    -1;

  time_t e=
    mktime(&t);

  if(
    e==(time_t)-1
  ){

    return false;
  }

  struct tm check;

  localtime_r(
    &e,
    &check
  );

  return

    check.tm_year==
    year-1900

    &&

    check.tm_mon==
    month-1

    &&

    check.tm_mday==
    day;
}


// =====================================================
//              CURRENT LOCAL CALENDAR
// =====================================================

static bool currentLocalYMD(
  int& day,
  int& month,
  int& year
){

  struct tm now;

  if(
    !getLocalTime(
      &now,
      500
    )
  ){

    return false;
  }

  day=
    now.tm_mday;

  month=
    now.tm_mon+1;

  year=
    now.tm_year+1900;

  return true;
}


// =====================================================
//                 SIMPLE TOKENIZER
// =====================================================

static bool calendarTokens(
  String q,
  String out[],
  int& count,
  int maxCount
){

  q=normalizeInput(q);

  count=0;

  int pos=0;

  while(
    pos<q.length() &&
    count<maxCount
  ){

    while(
      pos<q.length() &&
      q[pos]==' '
    ){

      pos++;
    }

    if(pos>=q.length()){

      break;
    }

    int end=
      q.indexOf(
        ' ',
        pos
      );

    if(end<0){

      end=
        q.length();
    }

    String token=
      q.substring(
        pos,
        end
      );

    token.trim();

    if(token.length()){

      out[count++]=
        token;
    }

    pos=
      end+1;
  }

  return
    count>0;
}


// =====================================================
//                 DATE EXPRESSION PARSER
// =====================================================

static ParsedCalendarDate parseCalendarDate(
  String input
){

  ParsedCalendarDate r={

    false,

    0,

    0,

    0,

    false,

    false
  };

  String q=
    normalizeInput(
      input
    );

  int todayDay=0;

  int todayMonth=0;

  int todayYear=0;

  if(
    !currentLocalYMD(
      todayDay,
      todayMonth,
      todayYear
    )
  ){

    return r;
  }


  // ===================================================
  // RELATIVE DATES
  // ===================================================

  int delta=999;

  if(
    q.indexOf(
      "day after tomorrow"
    )>=0
  ){

    delta=2;
  }

  else if(
    q.indexOf(
      "tomorrow"
    )>=0
  ){

    delta=1;
  }

  else if(
    q.indexOf(
      "yesterday"
    )>=0
  ){

    delta=-1;
  }

  else if(
    q.indexOf(
      "today"
    )>=0

    ||

    q.indexOf(
      "this date"
    )>=0
  ){

    delta=0;
  }


  if(delta!=999){

    struct tm t={};

    t.tm_year=
      todayYear-1900;

    t.tm_mon=
      todayMonth-1;

    t.tm_mday=
      todayDay+delta;

    t.tm_hour=
      12;

    t.tm_isdst=
      -1;

    time_t e=
      mktime(&t);

    if(
      e!=(time_t)-1
    ){

      struct tm out;

      localtime_r(
        &e,
        &out
      );

      r.valid=true;

      r.day=
        out.tm_mday;

      r.month=
        out.tm_mon+1;

      r.year=
        out.tm_year+1900;

      r.relative=true;

      return r;
    }
  }


  // ===================================================
  // NORMAL DATE EXPRESSIONS
  // ===================================================

  String tokens[24];

  int count=0;

  calendarTokens(
    q,
    tokens,
    count,
    24
  );


  // ---------------------------------------------------
  // Find a written month first.
  // ---------------------------------------------------

  int monthIndex=-1;

  int month=0;

  for(int i=0;i<count;i++){

    String clean=
      semanticCleanToken(
        tokens[i]
      );

    bool letters=false;

    for(size_t j=0;j<clean.length();j++){

      if(
        isalpha(
          (unsigned char)clean[j]
        )
      ){

        letters=true;

        break;
      }
    }

    if(!letters){

      continue;
    }

    int m=
      monthFromToken(
        clean
      );

    if(m>0){

      month=m;

      monthIndex=i;

      break;
    }
  }


  int day=-1;

  int year=-1;


  // ===================================================
  // WRITTEN MONTH
  // ===================================================

  if(monthIndex>=0){

    // Search around the month token for the day.

    for(
      int distance=1;
      distance<=3 &&
      day<0;
      distance++
    ){

      int left=
        monthIndex-distance;

      int right=
        monthIndex+distance;


      if(left>=0){

        int n=
          positiveNumberFromToken(
            tokens[left]
          );

        if(
          n>=1 &&
          n<=31
        ){

          day=n;
        }
      }


      if(
        day<0 &&
        right<count
      ){

        int n=
          positiveNumberFromToken(
            tokens[right]
          );

        if(
          n>=1 &&
          n<=31
        ){

          day=n;
        }
      }
    }


    // Search any token for an explicit year.

    for(int i=0;i<count;i++){

      int n=
        positiveNumberFromToken(
          tokens[i]
        );

      if(
        n>=1900 &&
        n<=2200
      ){

        year=n;

        r.explicitYear=true;

        break;
      }
    }
  }


  // ===================================================
  // NUMERIC DATE
  // ===================================================
  //
  // normalizeInput() changes:
  //
  // 29/08/2027
  //
  // into:
  //
  // 29 08 2027
  //
  // ===================================================

  else{

    int nums[5];

    int nc=0;

    for(
      int i=0;
      i<count &&
      nc<5;
      i++
    ){

      int n=
        positiveNumberFromToken(
          tokens[i]
        );

      if(n>=0){

        nums[nc++]=n;
      }
    }


    for(int i=0;i+1<nc;i++){

      int a=nums[i];

      int b=nums[i+1];


      // Day / month

      if(
        a>=1 &&
        a<=31 &&
        b>=1 &&
        b<=12
      ){

        day=a;

        month=b;
      }


      // Month / day,
      // used when second value cannot be month.

      else if(
        a>=1 &&
        a<=12 &&
        b>=13 &&
        b<=31
      ){

        month=a;

        day=b;
      }

      else{

        continue;
      }


      if(
        i+2<nc &&
        nums[i+2]>=1900 &&
        nums[i+2]<=2200
      ){

        year=
          nums[i+2];

        r.explicitYear=true;
      }

      break;
    }
  }


  if(
    day<1 ||
    month<1
  ){

    return r;
  }


  // If year was omitted,
  // use current local year.

  if(year<0){

    year=
      todayYear;
  }


  // "next 29 August"
  // means next occurrence.

  if(
    !r.explicitYear &&
    q.indexOf("next ")>=0
  ){

    if(
      month<todayMonth

      ||

      (
        month==todayMonth &&
        day<=todayDay
      )
    ){

      year++;
    }
  }


  if(
    !validCalendarDate(
      day,
      month,
      year
    )
  ){

    return r;
  }


  r.valid=true;

  r.day=day;

  r.month=month;

  r.year=year;

  return r;
}


// =====================================================
//                 WEEKDAY CALCULATION
// =====================================================

static String weekdayNameForDate(
  const ParsedCalendarDate& d
){

  if(!d.valid){

    return "";
  }

  struct tm t={};

  t.tm_year=
    d.year-1900;

  t.tm_mon=
    d.month-1;

  t.tm_mday=
    d.day;

  t.tm_hour=
    12;

  t.tm_isdst=
    -1;

  time_t e=
    mktime(&t);

  if(
    e==(time_t)-1
  ){

    return "";
  }

  struct tm out;

  localtime_r(
    &e,
    &out
  );

  const char* days[]={

    "Sunday",

    "Monday",

    "Tuesday",

    "Wednesday",

    "Thursday",

    "Friday",

    "Saturday"
  };

  return
    String(
      days[out.tm_wday]
    );
}


// =====================================================
//                 PRETTY DATE TEXT
// =====================================================

static String prettyCalendarDate(
  const ParsedCalendarDate& d
){

  if(!d.valid){

    return "";
  }

  return

    String(d.day)+

    " "+

    monthNameFull(
      d.month
    )+

    " "+

    String(
      d.year
    );
}


// =====================================================
//              CALENDAR QUESTION TYPE
// =====================================================

static bool asksWeekdayForDate(String q){

  q=normalizeInput(q);

  return

    q.indexOf(
      "what day"
    )>=0

    ||

    q.indexOf(
      "which day"
    )>=0

    ||

    q.indexOf(
      "weekday"
    )>=0

    ||

    q.indexOf(
      "day of week"
    )>=0

    ||

    q.indexOf(
      "falls on"
    )>=0

    ||

    q.indexOf(
      "fall on"
    )>=0;
}


// =====================================================
//                 DATE SIGNIFICANCE
// =====================================================

static bool asksDateSignificance(String q){

  q=normalizeInput(q);

  const char* clues[]={

    "significance",

    "significant",

    "special",

    "important",

    "celebrated",

    "celebrate",

    "observed",

    "observance",

    "holiday",

    "festival",

    "what happened",

    "happened on",

    "historical event",

    "history of this date",

    "events on"
  };

  for(
    size_t i=0;
    i<sizeof(clues)/sizeof(clues[0]);
    i++
  ){

    if(
      q.indexOf(
        clues[i]
      )>=0
    ){

      return true;
    }
  }

  return false;
}


static bool significanceWantsEvents(String q){

  q=normalizeInput(q);

  return

    q.indexOf(
      "what happened"
    )>=0

    ||

    q.indexOf(
      "happened on"
    )>=0

    ||

    q.indexOf(
      "historical"
    )>=0

    ||

    q.indexOf(
      "events on"
    )>=0;
}


static bool significanceWantsObservances(String q){

  q=normalizeInput(q);

  return

    q.indexOf(
      "celebrat"
    )>=0

    ||

    q.indexOf(
      "observ"
    )>=0

    ||

    q.indexOf(
      "holiday"
    )>=0

    ||

    q.indexOf(
      "festival"
    )>=0

    ||

    q.indexOf(
      "special"
    )>=0

    ||

    q.indexOf(
      "significance"
    )>=0

    ||

    q.indexOf(
      "significant"
    )>=0;
}


// =====================================================
//              WIKIPEDIA SECTION PARSER
// =====================================================

static String extractSectionText(
  String raw,
  String heading
){

  String lower=raw;

  lower.toLowerCase();

  String h=heading;

  h.toLowerCase();

  int pos=
    lower.indexOf(h);

  if(pos<0){

    return "";
  }

  int start=
    raw.indexOf(
      '\n',
      pos
    );

  if(start<0){

    start=
      pos+
      heading.length();
  }

  else{

    start++;
  }


  int next=
    lower.indexOf(
      "\n==",
      start
    );

  if(next<0){

    next=
      raw.length();
  }


  String section=
    raw.substring(
      start,
      next
    );

  section.trim();

  if(section.length()>2600){

    section=
      section.substring(
        0,
        2600
      );
  }

  return section;
}


static String stripListPrefix(String line){

  line.trim();

  while(line.length()){

    char c=
      line[0];

    if(
      c=='*' ||
      c=='#' ||
      c=='-' ||
      c==':' ||
      c==' '
    ){

      line.remove(
        0,
        1
      );

      line.trim();
    }

    else{

      break;
    }
  }

  return line;
}


// =====================================================
//           LOCATION / CONTEXT MATCHING
// =====================================================
//
// Example:
//
// significance of 15 august in india
//
// The query context contains "india".
//
// When possible, prefer lines from the calendar page
// that also mention India.
//
// This is generic; India itself is NOT hard-coded.
// =====================================================

static bool lineHasContextKeyword(
  String line,
  String query
){

  String qTokens[18];

  int qn=
    collectSemanticTokens(
      query,
      qTokens,
      18
    );


  const char* generic[]={

    "significance",

    "significant",

    "special",

    "important",

    "celebrate",

    "celebrated",

    "observance",

    "observed",

    "holiday",

    "festival",

    "happen",

    "happened",

    "historical",

    "event",

    "events",

    "day",

    "date",

    "weekday",

    "january",

    "february",

    "march",

    "april",

    "may",

    "june",

    "july",

    "august",

    "september",

    "october",

    "november",

    "december"
  };


  String nline=
    normalizeInput(
      line
    );


  for(int i=0;i<qn;i++){

    String t=
      qTokens[i];

    bool skip=false;


    for(
      size_t j=0;
      j<
      sizeof(generic)/
      sizeof(generic[0]);
      j++
    ){

      if(
        semanticTokenRelated(
          t,
          generic[j]
        )
      ){

        skip=true;

        break;
      }
    }


    if(skip){

      continue;
    }


    // Ignore numeric tokens.
    // Year is handled separately.

    bool numeric=true;

    for(size_t k=0;k<t.length();k++){

      if(
        !isdigit(
          (unsigned char)t[k]
        )
      ){

        numeric=false;

        break;
      }
    }

    if(numeric){

      continue;
    }


    if(
      nline.indexOf(t)>=0
    ){

      return true;
    }
  }

  return false;
}


// =====================================================
//             PICK USEFUL SIGNIFICANCE ITEMS
// =====================================================

static String selectSignificanceItems(
  String section,
  String query,
  const ParsedCalendarDate& d,
  int maxItems
){

  String chosen[5];

  int chosenCount=0;


  // ===================================================
  // FIRST PASS:
  // prefer explicit year or query context
  // ===================================================

  int start=0;

  while(
    start<section.length() &&
    chosenCount<maxItems
  ){

    int end=
      section.indexOf(
        '\n',
        start
      );

    if(end<0){

      end=
        section.length();
    }


    String line=
      stripListPrefix(
        section.substring(
          start,
          end
        )
      );

    start=
      end+1;


    if(line.length()<8){

      continue;
    }

    if(
      line.startsWith(
        "=="
      )
    ){

      continue;
    }


    bool strong=false;


    if(d.explicitYear){

      String y=
        String(
          d.year
        );

      strong=
        line.indexOf(y)>=0;
    }

    else{

      strong=
        lineHasContextKeyword(
          line,
          query
        );
    }


    if(strong){

      chosen[
        chosenCount++
      ]=
        line;
    }
  }


  // ===================================================
  // SECOND PASS:
  // no strong match, choose first useful entries
  // ===================================================

  if(chosenCount==0){

    start=0;


    while(
      start<section.length() &&
      chosenCount<maxItems
    ){

      int end=
        section.indexOf(
          '\n',
          start
        );

      if(end<0){

        end=
          section.length();
      }


      String line=
        stripListPrefix(
          section.substring(
            start,
            end
          )
        );

      start=
        end+1;


      if(line.length()<12){

        continue;
      }

      if(
        line.startsWith(
          "=="
        )
      ){

        continue;
      }


      chosen[
        chosenCount++
      ]=
        line;
    }
  }


  if(chosenCount==0){

    return "";
  }


  String out="";


  for(int i=0;i<chosenCount;i++){

    if(out.length()){

      out+="; ";
    }

    out+=
      chosen[i];
  }


  out.trim();


  if(out.length()>900){

    out=
      out.substring(
        0,
        900
      )+
      "...";
  }

  return out;
}


// =====================================================
//              FETCH DATE SIGNIFICANCE
// =====================================================
//
// Uses Wikipedia's calendar page:
//
//   August 29
//   January 26
//   December 31
//
// The month/day are generated by the parser.
// No individual dates are hard-coded.
// =====================================================

static bool fetchDateSignificance(
  const ParsedCalendarDate& d,
  String query,
  String& answer,
  String& source
){

  if(
    WiFi.status()
    !=
    WL_CONNECTED
  ){

    return false;
  }


  String title=

    monthNameFull(
      d.month
    )+

    " "+

    String(
      d.day
    );


  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=query"

    "&prop=extracts"

    "&explaintext=1"

    "&exsectionformat=plain"

    "&exchars=7000"

    "&redirects=1"

    "&format=json"

    "&formatversion=2"

    "&titles="+

    urlEncode(
      title
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

    Serial.print(
      "[CALENDAR WEB] HTTP "
    );

    Serial.println(
      code
    );

    return false;
  }


  String raw=
    extractJsonString(
      body,
      "extract"
    );


  if(raw.length()<80){

    return false;
  }


  String section="";

  String label="";


  // ---------------------------------------------------
  // HISTORY REQUEST
  // ---------------------------------------------------

  if(
    significanceWantsEvents(
      query
    )
  ){

    section=
      extractSectionText(
        raw,
        "Events"
      );

    label=
      "historical events";
  }


  // ---------------------------------------------------
  // HOLIDAYS / OBSERVANCES
  // ---------------------------------------------------

  if(
    !section.length() &&
    significanceWantsObservances(
      query
    )
  ){

    section=
      extractSectionText(
        raw,
        "Holidays and observances"
      );

    label=
      "observances";
  }


  // ---------------------------------------------------
  // GENERIC SIGNIFICANCE FALLBACK
  // ---------------------------------------------------

  if(!section.length()){

    section=
      extractSectionText(
        raw,
        "Events"
      );

    label=
      "historical events";
  }


  if(!section.length()){

    return false;
  }


  String items=
    selectSignificanceItems(
      section,
      query,
      d,
      3
    );


  if(!items.length()){

    return false;
  }


  answer=

    "For "+

    prettyCalendarDate(d)+

    ", Wikipedia's calendar page lists "+

    label+

    " such as: "+

    items;


  source=

    "Wikipedia calendar: "+

    title;


  return true;
}


// =====================================================
//                 CALENDAR ROUTER
// =====================================================

static bool handleCalendarUtility(
  const String& input
){

  String q=
    normalizeInput(
      input
    );


  ParsedCalendarDate d=
    parseCalendarDate(
      q
    );


  if(!d.valid){

    return false;
  }


  bool wantsWeekday=
    asksWeekdayForDate(
      q
    );


  bool wantsSignificance=
    asksDateSignificance(
      q
    );


  // Don't hijack a question just because it contains
  // a number that looks like a date.

  if(
    !wantsWeekday &&
    !wantsSignificance
  ){

    return false;
  }


  String weekday=
    weekdayNameForDate(
      d
    );


  String dateText=
    prettyCalendarDate(
      d
    );


  // ===================================================
  // WEEKDAY ONLY
  // ===================================================

  if(
    wantsWeekday &&
    !wantsSignificance
  ){

    Serial.println(
      "[CALENDAR] WEEKDAY CALCULATION"
    );


    elliSay(

      dateText+

      " falls on "+

      weekday+

      "."
    );


    return true;
  }


  // ===================================================
  // SIGNIFICANCE
  // ===================================================

  String significance="";

  String source="";


  bool gotSignificance=

    fetchDateSignificance(

      d,

      q,

      significance,

      source
    );


  // ===================================================
  // WEEKDAY + SIGNIFICANCE
  // ===================================================

  if(
    wantsWeekday &&
    gotSignificance
  ){

    Serial.println(
      "[CALENDAR] WEEKDAY + SIGNIFICANCE"
    );


    Serial.print(
      "[SOURCE] "
    );


    Serial.println(
      source
    );


    elliSay(

      dateText+

      " falls on "+

      weekday+

      ". "+

      significance
    );


    return true;
  }


  // ===================================================
  // SIGNIFICANCE ONLY
  // ===================================================

  if(gotSignificance){

    Serial.println(
      "[CALENDAR] DATE SIGNIFICANCE"
    );


    Serial.print(
      "[SOURCE] "
    );


    Serial.println(
      source
    );


    elliSay(
      significance
    );


    return true;
  }


  // ===================================================
  // WEB FAILURE FALLBACK
  // ===================================================

  if(wantsWeekday){

    elliSay(

      dateText+

      " falls on "+

      weekday+

      ". I couldn't fetch the date-significance source right now."
    );
  }

  else{

    elliSay(

      "I understood the date as "+

      dateText+

      ", but I couldn't fetch reliable significance information right now."
    );
  }


  return true;
}


// =====================================================
//               PRE-WEB SEMANTIC ROUTER
// =====================================================

bool handleDeterministicPreWeb(
  const String& input
){
  // ===================================================
  // PRIORITY 0:
  // CALENDAR / FESTIVAL / OBSERVANCE BRAIN
  // ===================================================
  //
  // This is deterministic routing and must beat AI/web.
  // Examples:
  //   what is on 25 december
  //   what day is 29 august
  //   when is teachers day in india
  //
  // ===================================================

  if(
    handleCalendarBrain(input)
  ){
    return true;
  }


  // ===================================================
  // PRIORITY 1:
  // CURRENT LOCAL TIME / DATE
  // ===================================================
  //
  // Examples:
  //   what is the date
  //   current date
  //   what time is it
  //   date and time
  //
  // These should NEVER be interpreted as definitions.
  // ===================================================

  if(
    handleLocalTemporalUtility(
      input
    )
  ){
    return true;
  }


  // ===================================================
  // PRIORITY 2:
  // ARBITRARY CALENDAR DATE
  // ===================================================

  if(
    handleCalendarUtility(
      input
    )
  ){
    return true;
  }


  return false;
}


// =====================================================
//               PRE-WEB SEMANTIC ROUTER
// =====================================================

bool handleSemanticPreWeb(
  const String& input
){
  // ===================================================
  // LOCAL-ACTION FIREWALL
  // ===================================================
  //
  // Semantic/web reasoning must never steal a command that belongs
  // to KIRA's clock, devices, tasks, routines, memory, or support
  // systems. Those handlers live later in the main local router.
  // ===================================================

  int utteranceIntent=
    detectUtteranceIntent(
      input
    );

  if(
    utteranceIntent==UTT_DEVICE ||
    utteranceIntent==UTT_CLOCK ||
    utteranceIntent==UTT_TASK ||
    utteranceIntent==UTT_ROUTINE ||
    utteranceIntent==UTT_PROFILE ||
    utteranceIntent==UTT_SUPPORT
  ){
    return false;
  }

  // Keep this call so old code paths remain safe even if they call
  // handleSemanticPreWeb() directly. The main dispatcher also calls
  // handleDeterministicPreWeb() before KIRA v1 AI routing.
  if(
    handleDeterministicPreWeb(
      input
    )
  ){
    return true;
  }


  // ===================================================
  // PRIORITY 3:
  // GENERAL SEMANTIC ANALYSIS
  // ===================================================

  SemanticFrame f=
    analyzeSemanticQuery(
      input
    );


  // ---------------------------------------------------
  // FRESH WEB FACT REQUEST
  // ---------------------------------------------------

  if(
    f.kind==
    SEM_FACT_REQUEST
  ){

    String answer;

    String source;

    int confidence=0;


    Serial.print(
      "[FACT] Web topic: "
    );


    Serial.println(

      f.topic.length()

      ?

      f.topic

      :

      "<random>"
    );


    if(
      webRandomFact(

        f.topic,

        answer,

        source,

        confidence
      )
    ){

      Serial.print(
        "[SOURCE] "
      );


      Serial.println(
        source
      );


      Serial.print(
        "[CONFIDENCE] "
      );


      Serial.println(
        confidence
      );


      elliSay(
        answer
      );
    }

    else{

      elliSay(

        "I couldn't fetch a fresh web fact right now. "

        "I won't pretend a small built-in list is a live fact source."
      );
    }

    return true;
  }


  // ---------------------------------------------------
  // SUBJECTIVE RANKING WITHOUT CRITERION
  // ---------------------------------------------------

  if(
    f.kind==
      SEM_RANKING

    &&

    f.subjective

    &&

    !f.hasCriterion
  ){

    elliSay(

      "That ranking depends on the criterion. "

      "Tell me what you want to rank by, or name "

      "the ranking/source you want me to use."
    );


    return true;
  }


  return false;
}


// =====================================================
//                CONCEPT VARIANTS
// =====================================================

void addVariant(
  String v,
  String out[],
  int& count,
  int maxCount
){

  v=normalizeInput(v);

  v.trim();


  if(
    !v.length() ||
    count>=maxCount
  ){

    return;
  }


  for(int i=0;i<count;i++){

    if(out[i]==v){

      return;
    }
  }


  out[count++]=v;
}


int buildConceptVariants(
  String term,
  String out[],
  int maxCount
){

  term=normalizeInput(term);

  term.trim();


  int count=0;


  addVariant(

    term,

    out,

    count,

    maxCount
  );


  // ---------------------------------------------------
  // SPACE FORM
  // ---------------------------------------------------

  String spaceForm=
    term;


  spaceForm.replace(
    "-",
    " "
  );


  while(
    spaceForm.indexOf("  ")>=0
  ){

    spaceForm.replace(
      "  ",
      " "
    );
  }


  addVariant(

    spaceForm,

    out,

    count,

    maxCount
  );


  // ---------------------------------------------------
  // JOINED FORM
  // ---------------------------------------------------

  String joined=
    spaceForm;


  joined.replace(
    " ",
    ""
  );


  addVariant(

    joined,

    out,

    count,

    maxCount
  );


  // ---------------------------------------------------
  // HYPHENATED FORM
  // ---------------------------------------------------

  String hyphenated=
    spaceForm;


  hyphenated.replace(
    " ",
    "-"
  );


  addVariant(

    hyphenated,

    out,

    count,

    maxCount
  );


  // ---------------------------------------------------
  // GENERIC ADJACENT JOINING
  // ---------------------------------------------------

  String words[5];

  int wc=0;

  int pos=0;


  while(
    pos<spaceForm.length() &&
    wc<5
  ){

    while(
      pos<spaceForm.length() &&
      spaceForm[pos]==' '
    ){

      pos++;
    }


    if(
      pos>=spaceForm.length()
    ){

      break;
    }


    int end=
      spaceForm.indexOf(
        ' ',
        pos
      );


    if(end<0){

      end=
        spaceForm.length();
    }


    words[wc++]=
      spaceForm.substring(
        pos,
        end
      );


    pos=
      end+1;
  }


  if(
    wc>=2 &&
    wc<=4
  ){

    for(
      int joinAt=0;
      joinAt<wc-1 &&
      count<maxCount;
      joinAt++
    ){

      String v="";


      for(int i=0;i<wc;i++){

        if(
          i>0 &&
          i!=joinAt+1
        ){

          v+=" ";
        }


        v+=
          words[i];
      }


      addVariant(

        v,

        out,

        count,

        maxCount
      );
    }
  }


  return count;
}


// =====================================================
//             DEFINITION SUBJECT FOCUS
// =====================================================

String semanticFocusDefinition(
  String term,
  String extract
){

  String variants[8];


  int vc=
    buildConceptVariants(

      term,

      variants,

      8
    );


  int start=0;


  for(
    int sentence=0;
    sentence<5 &&
    start<extract.length();
    sentence++
  ){

    int end=start;


    while(
      end<extract.length()
    ){

      char c=
        extract[end];


      if(
        c=='.' ||
        c=='!' ||
        c=='?'
      ){

        end++;

        break;
      }


      end++;
    }


    String s=
      extract.substring(

        start,

        end
      );


    String normalized=
      normalizeInput(s);


    for(int i=0;i<vc;i++){

      if(
        normalized.indexOf(
          variants[i]
        )>=0
      ){

        int next=end;


        while(
          next<extract.length() &&
          extract[next]==' '
        ){

          next++;
        }


        int nextEnd=next;


        while(
          nextEnd<extract.length()
        ){

          char c=
            extract[nextEnd];


          if(
            c=='.' ||
            c=='!' ||
            c=='?'
          ){

            nextEnd++;

            break;
          }


          nextEnd++;
        }


        String out=s;


        if(
          nextEnd>next

          &&

          out.length()+
          (nextEnd-next)

          <
          620
        ){

          out+=" ";


          out+=
            extract.substring(

              next,

              nextEnd
            );
        }


        out.trim();


        return out;
      }
    }


    start=end;


    while(
      start<extract.length() &&
      extract[start]==' '
    ){

      start++;
    }
  }


  return "";
}


// =====================================================
//              COMPARISON EVIDENCE
// =====================================================

bool containsComparisonEvidence(
  String text
){

  text=
    normalizeInput(text);


  String words[40];


  int wc=
    collectSemanticTokens(

      text,

      words,

      40
    );


  for(int i=0;i<wc;i++){

    String w=
      words[i];


    if(
      w=="rank" ||

      w=="ranking" ||

      w=="ranked" ||

      w=="maximum" ||

      w=="minimum" ||

      w=="most" ||

      w=="least" ||

      w=="best" ||

      w=="worst" ||

      w=="top" ||

      tokenIsMaximumOperator(w) ||

      tokenIsMinimumOperator(w) ||

      tokenLooksSuperlative(w)
    ){

      return true;
    }
  }


  return false;
}


// =====================================================
//                EVIDENCE SCORING
// =====================================================

int semanticEvidenceScore(
  String query,
  String title,
  String candidate
){

  SemanticFrame f=
    analyzeSemanticQuery(
      query
    );


  String qTokens[20];

  String tTokens[20];

  String cTokens[48];


  int qn=
    collectSemanticTokens(

      f.subject.length()

      ?

      f.subject

      :

      query,

      qTokens,

      20
    );


  int tn=
    collectSemanticTokens(

      title,

      tTokens,

      20
    );


  int cn=
    collectSemanticTokens(

      candidate,

      cTokens,

      48
    );


  if(qn==0){

    return 20;
  }


  int titleHits=0;

  int bodyHits=0;


  for(int i=0;i<qn;i++){

    bool inTitle=false;

    bool inBody=false;


    for(int j=0;j<tn;j++){

      if(
        semanticTokenRelated(

          qTokens[i],

          tTokens[j]
        )
      ){

        inTitle=true;

        break;
      }
    }


    for(int j=0;j<cn;j++){

      if(
        semanticTokenRelated(

          qTokens[i],

          cTokens[j]
        )
      ){

        inBody=true;

        break;
      }
    }


    if(inTitle){

      titleHits++;
    }


    if(inBody){

      bodyHits++;
    }
  }


  int score=0;


  score+=

    (titleHits*35)

    /

    qn;


  score+=

    (bodyHits*40)

    /

    qn;


  String nTitle=
    normalizeInput(
      title
    );


  String nSubject=
    normalizeInput(
      f.subject
    );


  if(
    nSubject.length() &&
    nTitle==nSubject
  ){

    score+=20;
  }


  else if(
    nSubject.length() &&
    nTitle.indexOf(
      nSubject
    )>=0
  ){

    score+=12;
  }


  if(
    (
      f.kind==
      SEM_RANKING

      ||

      f.kind==
      SEM_COMPARISON
    )

    &&

    containsComparisonEvidence(

      title+

      " "+

      candidate
    )
  ){

    score+=15;
  }


  if(score>100){

    score=100;
  }


  return score;
}


// =====================================================
//             REQUIRED EVIDENCE LEVEL
// =====================================================

int semanticRequiredEvidence(
  String query
){

  SemanticFrame f=
    analyzeSemanticQuery(
      query
    );


  switch(f.kind){

    case SEM_RANKING:

      return 58;


    case SEM_COMPARISON:

      return 55;


    case SEM_DEFINITION:

      return 42;


    case SEM_LOOKUP:

      return 34;


    default:

      return 32;
  }
}


// =====================================================
//             FINAL SOURCE ACCEPTANCE
// =====================================================

bool semanticCandidateAcceptable(
  String query,
  String title,
  String candidate
){

  int score=
    semanticEvidenceScore(

      query,

      title,

      candidate
    );


  int need=
    semanticRequiredEvidence(
      query
    );


  return
    score>=need;
}