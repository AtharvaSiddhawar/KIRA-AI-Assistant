#include <Arduino.h>
#include "elli_query_frame.h"

namespace KiraV1 {

static String normalizeText(String s){
  s.toLowerCase();
  String out="";
  bool sp=false;
  for(size_t i=0;i<s.length();i++){
    char c=s[i];
    if(isalnum((unsigned char)c) || c=='-' || c=='_'){
      out+=c; sp=false;
    }else if(out.length() && !sp){
      out+=' '; sp=true;
    }
  }
  out.trim();
  while(out.indexOf("  ")>=0) out.replace("  "," ");
  return out;
}

static int tokenize(const String& input,String out[],int maxCount){
  String s=normalizeText(input);
  int count=0,pos=0;
  while(pos<s.length() && count<maxCount){
    while(pos<s.length() && s[pos]==' ') pos++;
    if(pos>=s.length()) break;
    int end=s.indexOf(' ',pos);
    if(end<0) end=s.length();
    out[count++]=s.substring(pos,end);
    pos=end+1;
  }
  return count;
}

static bool isFunctionWord(const String& w){
  const char* const a[]={
    "a","an","the",

    "is","are","was","were","be","been","being",
    "do","does","did",
    "has","have","had",
    "can","could","would","should","will",

    "what","which","who","where","when","why","how",
    "tell","give","show","me",

    "this","that","these","those","it","its",
    "please",

    "current","currently","now","today",

    "located","location",

    "with","according"
  };

  for(
    size_t i=0;
    i<sizeof(a)/sizeof(a[0]);
    i++
  ){
    if(w==a[i]){
      return true;
    }
  }

  return false;
}

static bool isRelationWord(const String& w){
  return
    w=="in" ||
    w=="at" ||
    w=="of" ||
    w=="for" ||
    w=="near" ||
    w=="around" ||
    w=="within" ||
    w=="by" ||
    w=="among";
}

static String joinRange(String words[],int count,int start,int end,bool dropRelations=false){
  start=max(0,start); end=min(count,end);
  String out="";
  for(int i=start;i<end;i++){
    if(isFunctionWord(words[i])) continue;
    if(dropRelations && isRelationWord(words[i])) continue;
    if(out.length()) out+=" ";
    out+=words[i];
  }
  out.trim();
  return out;
}

static int findToken(String words[],int count,const char* token){
  for(int i=0;i<count;i++) if(words[i]==token) return i;
  return -1;
}

static bool isWeatherWord(const String& w){
  const char* const a[]={"weather","temperature","forecast","rain","raining","rainy","humidity","humid","wind","windy","hot","cold"};
  for(size_t i=0;i<sizeof(a)/sizeof(a[0]);i++) if(w==a[i]) return true;
  return false;
}

static bool hasWeatherSignal(String words[],int count){
  for(int i=0;i<count;i++) if(isWeatherWord(words[i])) return true;
  return false;
}

static String weatherMetric(String words[],int count){
  for(int i=0;i<count;i++){
    if(words[i]=="temperature" || words[i]=="hot" || words[i]=="cold") return "temperature";
    if(words[i]=="rain" || words[i]=="raining" || words[i]=="rainy") return "precipitation";
    if(words[i]=="humidity" || words[i]=="humid") return "humidity";
    if(words[i]=="wind" || words[i]=="windy") return "wind";
  }
  return "weather";
}

static String weatherLocation(String words[],int count){
  // Prefer explicit relation after a weather term: weather OF Yavatmal,
  // temperature AT Pune, forecast FOR Delhi, etc.
  for(int i=0;i<count;i++){
    if(!isWeatherWord(words[i])) continue;
    for(int j=i+1;j<count;j++){
      if(isRelationWord(words[j])){
        String v=joinRange(words,count,j+1,count,true);
        if(v.length()) return v;
      }
    }
  }

  // Handles "how hot is yavatmal" and "is yavatmal raining" by
  // removing grammar/weather words and retaining the place phrase.
  String out="";
  for(int i=0;i<count;i++){
    if(isFunctionWord(words[i]) || isRelationWord(words[i]) || isWeatherWord(words[i])) continue;
    if(out.length()) out+=" ";
    out+=words[i];
  }
  out.trim();
  return out;
}

static bool isMaxWord(const String& w){
  return w=="most" || w=="maximum" || w=="highest" || w=="largest" || w=="biggest" || w=="greatest" || w=="fastest" || w=="oldest" || w=="longest" || w=="richest";
}

static bool isMinWord(const String& w){
  return w=="least" || w=="minimum" || w=="lowest" || w=="smallest" || w=="slowest" || w=="youngest" || w=="shortest" || w=="poorest";
}

static String inferMetric(const String& w){

  if(
    w=="fastest" ||
    w=="slowest"
  ){
    return "speed";
  }

  if(
    w=="oldest" ||
    w=="youngest"
  ){
    return "age";
  }

  if(
    w=="richest" ||
    w=="poorest"
  ){
    return "wealth";
  }

  if(
    w=="largest" ||
    w=="biggest" ||
    w=="smallest"
  ){
    return "size";
  }

  if(
    w=="highest" ||
    w=="lowest"
  ){
    return "height";
  }

  return "";
}

static String canonicalMetric(const String& w){
  if(w=="populated" || w=="populous" || w=="population") return "population";
  return w;
}

static bool startsDefinition(const String& q){
  const char* const a[]={"what is ","what are ","what is meant by ","define ","explain ","tell me about ","meaning of "};
  for(size_t i=0;i<sizeof(a)/sizeof(a[0]);i++) if(q.startsWith(a[i])) return true;
  return false;
}

static String stripDefinitionPrefix(String q){
  const char* const a[]={"what is meant by ","what is ","what are ","define ","explain ","tell me about ","meaning of "};
  for(size_t i=0;i<sizeof(a)/sizeof(a[0]);i++){
    String p=a[i];
    if(q.startsWith(p)){ q.remove(0,p.length()); break; }
  }
  q.trim(); return q;
}

void clearQueryFrame(QueryFrame& f){
  f.raw=""; f.normalized=""; f.intent=INTENT_UNKNOWN; f.op=OP_NONE; f.expectedAnswer=ANSWER_TEXT;
  f.subject=""; f.entity=""; f.relation=""; f.location=""; f.region=""; f.metric=""; f.criterion="";
  f.needsCurrentData=false; f.needsWeb=false; f.ambiguous=false; f.confidence=0;
}

bool analyzeQuery(const String& input,QueryFrame& f){
  clearQueryFrame(f);
  f.raw=input;
  f.normalized=normalizeText(input);
  String words[48];
  int count=tokenize(f.normalized,words,48);
  if(count==0) return false;

  if(hasWeatherSignal(words,count)){
    f.intent=INTENT_CURRENT_WEATHER;
    f.expectedAnswer=ANSWER_WEATHER;
    f.needsCurrentData=true;
    f.needsWeb=true;
    f.metric=weatherMetric(words,count);
    f.location=weatherLocation(words,count);
    f.subject=f.location;
    f.relation="current_weather";
    f.ambiguous=!f.location.length();
    f.confidence=f.ambiguous?70:95;
    return true;
  }

  // Location is a requested RELATION. The entity is everything useful
  // left after grammar words; we do not require one exact phrase shape.
  if(words[0]=="where" || f.normalized.indexOf("location of ")>=0 || f.normalized.indexOf("located")>=0){
    f.intent=INTENT_ENTITY_LOCATION;
    f.expectedAnswer=ANSWER_LOCATION;
    f.relation="located_in";
    f.needsWeb=true;
    f.entity=joinRange(words,count,0,count,true);
    f.subject=f.entity;
    f.ambiguous=!f.entity.length();
    f.confidence=f.ambiguous?55:94;
    return true;
  }

  bool ranking=false;
  int opIndex=-1;
  for(int i=0;i<count;i++){
    if(words[i]=="best" || words[i]=="top"){ f.op=OP_BEST; ranking=true; opIndex=i; break; }
    if(words[i]=="worst"){ f.op=OP_WORST; ranking=true; opIndex=i; break; }
    if(isMaxWord(words[i])){ f.op=OP_MAX; ranking=true; opIndex=i; break; }
    if(isMinWord(words[i])){ f.op=OP_MIN; ranking=true; opIndex=i; break; }
  }

  if(ranking){

    f.intent=
      INTENT_RANKING;

    f.expectedAnswer=
      ANSWER_ENTITY;

    f.needsWeb=true;

    f.relation=
      "rank_by_metric";


    // ===================================================
    // REGION
    //
    // Examples:
    //
    //   most populated city in usa
    //   largest city within india
    // ===================================================

    int regionAt=
      findToken(
        words,
        count,
        "in"
      );

    if(regionAt<0){

      regionAt=
        findToken(
          words,
          count,
          "within"
        );
    }

    if(regionAt>=0){

      f.region=
        joinRange(
          words,
          count,
          regionAt+1,
          count,
          true
        );
    }


    // ===================================================
    // DEFAULT METRIC FROM OPERATOR
    // ===================================================

    f.metric=
      inferMetric(
        words[opIndex]
      );


    // ===================================================
    // METRIC DIRECTLY AFTER OPERATOR
    //
    //   most POPULATED city
    //   highest POPULATION city
    //   fastest animal
//
// For BEST/WORST, the next word is usually part of the target noun
// phrase (for example "best lip gloss"), not a metric.
    // ===================================================

    if(
      opIndex+1<count &&
      f.op!=OP_BEST &&
      f.op!=OP_WORST
    ){

      String next=
        canonicalMetric(
          words[opIndex+1]
        );


      const char* const entityClasses[]={

        "city",
        "country",
        "state",
        "province",

        "university",
        "college",
        "school",

        "company",
        "organization",

        "person",
        "scientist",
        "actor",

        "animal",
        "bird",
        "fish",

        "planet",
        "star",

        "river",
        "lake",
        "ocean",

        "mountain",
        "building",

        "car",
        "vehicle",
        "airport"
      };


      bool looksLikeEntityClass=false;


      for(
        size_t i=0;
        i<sizeof(entityClasses)/sizeof(entityClasses[0]);
        i++
      ){

        if(
          next==
          entityClasses[i]
        ){

          looksLikeEntityClass=true;

          break;
        }
      }


      if(
        !looksLikeEntityClass &&
        !isFunctionWord(next) &&
        !isRelationWord(next)
      ){

        f.metric=
          next;
      }
    }


    // ===================================================
    // EXPLICIT "BY ..." METRIC / CRITERION
    //
    //   largest city by population
    //   richest country by GDP
    //   best university by QS ranking
    // ===================================================

    int byAt=
      findToken(
        words,
        count,
        "by"
      );


    if(byAt>=0){

      int metricEnd=

        regionAt>=0 &&
        regionAt>byAt

        ?

        regionAt

        :

        count;


      String byPhrase=
        joinRange(
          words,
          count,
          byAt+1,
          metricEnd,
          true
        );


      if(
        f.op==OP_BEST ||
        f.op==OP_WORST
      ){

        f.criterion=
          byPhrase;
      }

      else if(
        byPhrase.length()
      ){

        f.metric=
          canonicalMetric(
            byPhrase
          );
      }
    }


    // ===================================================
    // TARGET ENTITY CLASS
    //
    // Example:
    //
    //   which is the most populated city in usa
    //
    // Remove:
    //   which / is / the / most / populated
    //
    // Keep:
    //   city
    // ===================================================

    int subjectEnd=
      count;


    if(regionAt>=0){

      subjectEnd=
        min(
          subjectEnd,
          regionAt
        );
    }


    if(byAt>=0){

      subjectEnd=
        min(
          subjectEnd,
          byAt
        );
    }


    String subject="";


    for(
      int i=0;
      i<subjectEnd;
      i++
    ){

      String word=
        words[i];


      // Grammar / relation words.
      if(
        isFunctionWord(word) ||
        isRelationWord(word)
      ){

        continue;
      }


      // Ranking operator.
      if(
        word=="best" ||
        word=="top" ||
        word=="worst" ||
        isMaxWord(word) ||
        isMinWord(word)
      ){

        continue;
      }


      // Metric itself.
      String canonical=
        canonicalMetric(
          word
        );


      if(
        f.metric.length() &&
        canonical==
        f.metric
      ){

        continue;
      }


      // Ranking filler.
      if(
        word=="rank" ||
        word=="ranked" ||
        word=="ranking"
      ){

        continue;
      }


      if(subject.length()){

        subject+=" ";
      }


      subject+=word;
    }


    subject.trim();


    f.subject=
      subject;

    f.entity=
      subject;


    // ===================================================
    // AMBIGUITY
    //
    // Objective:
    //   most populated
    //   highest GDP
    //   fastest
    //
    // already has a metric.
    //
    // Subjective:
    //   best university
    //
    // may use an explicit criterion, otherwise "best" means overall.
    // ===================================================

    // BEST/WORST without an explicit criterion now means an overall
    // comparison. It is ambiguous only when the target itself is missing.
    f.ambiguous=
      !f.subject.length();


    f.confidence=

      f.subject.length()

      ?

      94

      :

      75;


    return true;
  }


  if(startsDefinition(f.normalized)){
    f.intent=INTENT_DEFINITION;
    f.expectedAnswer=ANSWER_TEXT;
    f.needsWeb=true;
    f.subject=stripDefinitionPrefix(f.normalized);
    f.entity=f.subject;
    f.relation="definition";
    f.ambiguous=!f.subject.length();
    f.confidence=f.ambiguous?55:94;
    return true;
  }

  f.intent=INTENT_FACT_LOOKUP;
  f.expectedAnswer=ANSWER_TEXT;
  f.needsWeb=true;
  f.subject=f.normalized;
  f.entity=f.subject;
  f.relation="lookup";
  f.confidence=65;
  return true;
}

const char* intentName(IntentType i){
  switch(i){
    case INTENT_DEFINITION:return "DEFINITION";
    case INTENT_ENTITY_LOCATION:return "ENTITY LOCATION";
    case INTENT_CURRENT_WEATHER:return "CURRENT WEATHER";
    case INTENT_RANKING:return "RANKING";
    case INTENT_COMPARISON:return "COMPARISON";
    case INTENT_FACT_LOOKUP:return "FACT LOOKUP";
    default:return "UNKNOWN";
  }
}

const char* operatorName(QueryOperator op){
  switch(op){
    case OP_MAX:return "MAX"; case OP_MIN:return "MIN"; case OP_BEST:return "BEST";
    case OP_WORST:return "WORST"; case OP_COMPARE:return "COMPARE"; default:return "NONE";
  }
}

const char* answerTypeName(AnswerType t){
  switch(t){
    case ANSWER_LOCATION:return "LOCATION"; case ANSWER_WEATHER:return "WEATHER";
    case ANSWER_NUMBER:return "NUMBER"; case ANSWER_ENTITY:return "ENTITY"; default:return "TEXT";
  }
}

void printQueryFrame(const QueryFrame& f){
  Serial.println(); Serial.println("========== KIRA QUERY FRAME v1 ==========");
  Serial.print("Intent      : "); Serial.println(intentName(f.intent));
  Serial.print("Subject     : "); Serial.println(f.subject);
  Serial.print("Entity      : "); Serial.println(f.entity);
  Serial.print("Relation    : "); Serial.println(f.relation);
  Serial.print("Location    : "); Serial.println(f.location);
  Serial.print("Region      : "); Serial.println(f.region);
  Serial.print("Metric      : "); Serial.println(f.metric);
  Serial.print("Criterion   : "); Serial.println(f.criterion);
  Serial.print("Operator    : "); Serial.println(operatorName(f.op));
  Serial.print("Expected    : "); Serial.println(answerTypeName(f.expectedAnswer));
  Serial.print("Current data: "); Serial.println(f.needsCurrentData?"YES":"NO");
  Serial.print("Ambiguous   : "); Serial.println(f.ambiguous?"YES":"NO");
  Serial.print("Confidence  : "); Serial.println(f.confidence);
  Serial.println("=========================================");
}

} // namespace KiraV1
