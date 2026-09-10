#include <Arduino.h>
#include "elli_nlu.h"

// Still lives in kira_brain.ino during modular migration.
String normalizeInput(String s);

// =====================================================
// SMALL TOKEN HELPERS
// =====================================================

static bool nluWordEqualsAny(
  const String& word,
  const char* const values[],
  size_t count
){
  for(size_t i=0;i<count;i++){
    if(word==values[i]) return true;
  }
  return false;
}


static bool nluContainsToken(
  String text,
  const String& token
){
  text=normalizeInput(text);

  if(text==token) return true;
  if(text.startsWith(token+" ")) return true;
  if(text.endsWith(" "+token)) return true;
  return text.indexOf(" "+token+" ")>=0;
}


static bool nluContainsPhrase(
  String text,
  const String& phrase
){
  text=" "+normalizeInput(text)+" ";
  String needle=" "+normalizeInput(phrase)+" ";
  return text.indexOf(needle)>=0;
}


static int nluSplit(
  const String& q,
  String words[],
  int maximum
){
  int count=0;
  int pos=0;

  while(
    pos<q.length() &&
    count<maximum
  ){
    while(
      pos<q.length() &&
      q[pos]==' '
    ) pos++;

    if(pos>=q.length()) break;

    int end=q.indexOf(' ',pos);
    if(end<0) end=q.length();

    words[count++]=q.substring(pos,end);
    pos=end+1;
  }

  return count;
}


static bool nluIsWhWord(const String& w){
  const char* const a[]={
    "what","which","who","whom","whose",
    "where","when","why","how"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluIsAuxiliary(const String& w){
  const char* const a[]={
    "am","is","are","was","were",
    "do","does","did",
    "have","has","had",
    "can","could","may","might","must",
    "shall","should","will","would"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluIsModal(const String& w){
  const char* const a[]={
    "can","could","may","might","must",
    "shall","should","will","would"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluIsFirstPerson(const String& w){
  const char* const a[]={
    "i","me","my","mine","myself",
    "we","us","our","ours","ourselves"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluIsSecondPerson(const String& w){
  const char* const a[]={
    "you","your","yours","yourself","yourselves"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluIsThirdPerson(const String& w){
  const char* const a[]={
    "he","him","his","himself",
    "she","her","hers","herself",
    "they","them","their","theirs","themselves",
    "it","its","itself"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluIsNegation(const String& w){
  const char* const a[]={
    "not","never","no","none","nobody","nothing",
    "neither","nor","cannot","hardly","barely"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluIsConjunction(const String& w){
  const char* const a[]={
    "and","but","or","nor","yet","so",
    "because","although","though","while","whereas",
    "if","unless","when","whenever","before","after",
    "until","since","therefore","however","then"
  };
  return nluWordEqualsAny(w,a,sizeof(a)/sizeof(a[0]));
}


static bool nluLooksLikeVerbForm(const String& w){
  if(w.length()<2) return false;

  if(nluIsAuxiliary(w)) return true;

  const char* const common[]={
    "be","been","being",
    "go","goes","went","gone",
    "come","comes","came",
    "get","gets","got","gotten",
    "make","makes","made",
    "say","says","said",
    "tell","tells","told",
    "know","knows","knew","known",
    "think","thinks","thought",
    "want","wants","wanted",
    "need","needs","needed",
    "like","likes","liked",
    "love","loves","loved",
    "prefer","prefers","preferred",
    "study","studies","studied",
    "learn","learns","learned",
    "work","works","worked",
    "live","lives","lived",
    "play","plays","played",
    "use","uses","used",
    "own","owns","owned",
    "speak","speaks","spoke","spoken",
    "read","reads",
    "write","writes","wrote","written",
    "feel","feels","felt"
  };

  if(nluWordEqualsAny(w,common,sizeof(common)/sizeof(common[0]))) return true;

  // Productive English morphology. These are only structural hints,
  // never automatic spelling corrections.
  if(w.length()>4 && w.endsWith("ing")) return true;
  if(w.length()>3 && w.endsWith("ed")) return true;

  return false;
}


// =====================================================
// QUESTION / COMMAND STRUCTURE
// =====================================================

static bool nluPoliteKnowledgeDirective(String q){

  q=
    normalizeInput(
      q
    );


  // ---------------------------------------------------
  // DISCOURSE STARTERS
  //
  // Spoken English often starts with filler words:
  //
  //   well suggest me something
  //   okay recommend something
  //   so tell me about ...
  //
  // These words should not change the sentence's intent.
  // ---------------------------------------------------

  const char* const fillers[]={

    "well ",
    "okay ",
    "ok ",
    "alright ",
    "so ",
    "hmm ",
    "actually ",
    "basically ",
    "please "
  };


  bool removed=true;


  while(removed){

    removed=false;


    for(
      size_t i=0;
      i<
      sizeof(fillers)/
      sizeof(fillers[0]);
      i++
    ){

      String f=
        String(
          fillers[i]
        );


      if(
        q.startsWith(
          f
        )
      ){

        q=
          q.substring(
            f.length()
          );

        q.trim();

        removed=true;

        break;
      }
    }
  }


  const char* const starts[]={

    "tell me ",
    "show me ",
    "give me ",

    "explain ",
    "define ",
    "describe ",
    "compare ",
    "calculate ",
    "compute ",
    "identify ",
    "name ",
    "list ",
    "find ",
    "look up ",
    "search for ",
    "check ",

    // Advice / recommendation language.
    "suggest ",
    "suggest me ",
    "recommend ",
    "recommend me ",
    "advise ",
    "advise me ",
    "give me an idea",
    "give me some ideas",
    "give me a suggestion",
    "give me some suggestions",

    "help me ",
    "help me understand ",

    "i want to know ",
    "i would like to know ",
    "i wonder ",

    "do you know ",
    "can you tell ",
    "could you tell ",
    "would you tell ",
    "will you tell ",

    "can you explain ",
    "could you explain ",
    "would you explain ",

    "can you suggest ",
    "could you suggest ",
    "would you suggest ",

    "can you recommend ",
    "could you recommend ",
    "would you recommend ",

    "what do you suggest",
    "what would you suggest",
    "what do you recommend",
    "what would you recommend",

    "please explain ",
    "please tell ",
    "please show ",
    "please find ",
    "please suggest ",
    "please recommend "
  };


  for(
    size_t i=0;
    i<
    sizeof(starts)/
    sizeof(starts[0]);
    i++
  ){

    if(
      q.startsWith(
        starts[i]
      )
    ){

      return true;
    }
  }


  return false;
}


static bool nluGeneralQuestion(String q,String words[],int count){
  if(!count) return false;

  if(nluPoliteKnowledgeDirective(q)) return true;

  if(nluIsWhWord(words[0])) return true;

  // Preposition + WH constructions:
  //   in which year ...
  //   by whom ...
  //   for what reason ...
  //   from where ...
  const char* const prepositions[]={
    "in","on","at","by","for","from","to","with","about","under","over"
  };

  if(count>=2){
    bool firstIsPrep=nluWordEqualsAny(
      words[0],
      prepositions,
      sizeof(prepositions)/sizeof(prepositions[0])
    );

    if(firstIsPrep && nluIsWhWord(words[1])) return true;
  }

  // Introductory qualifier followed quickly by a WH clause:
  //   according to science what ...
  //   in your view why ...
  // Keep this narrow so declaratives such as "I know what you mean"
  // are not misclassified as questions.
  for(int i=1;i<count && i<=4;i++){
    if(nluIsWhWord(words[i])){
      if(
        words[0]=="according" ||
        words[0]=="regarding" ||
        words[0]=="concerning" ||
        words[0]=="about"
      ) return true;
    }
  }

  if(nluIsAuxiliary(words[0])){
    // "is yavatmal raining", "could this work", "have we ..."
    return true;
  }

  // Polite modal inversion: "could you please explain..."
  if(
    count>=2 &&
    nluIsModal(words[0]) &&
    words[1]=="you"
  ) return true;

  // Embedded interrogatives.
  if(
    q.startsWith("i wonder if ") ||
    q.startsWith("i wonder whether ") ||
    q.startsWith("i wonder why ") ||
    q.startsWith("i wonder how ") ||
    q.startsWith("i wonder where ") ||
    q.startsWith("i wonder when ")
  ) return true;

  return false;
}


static bool nluGeneralCommand(String q,String words[],int count){
  if(!count) return false;

  // Knowledge directives are routed as questions, not actuator commands.
  if(nluPoliteKnowledgeDirective(q)) return false;

  const char* const imperativeVerbs[]={
    "turn","switch","power","set","start","stop","pause","resume",
    "cancel","delete","remove","add","create","open","close","play",
    "send","call","write","read","save","store","remember","forget",
    "remind","schedule","enable","disable","connect","disconnect",
    "increase","decrease","raise","lower","mute","unmute","silence",
    "wake","count","run","launch","restart","reset","clear"
  };

  if(nluWordEqualsAny(words[0],imperativeVerbs,sizeof(imperativeVerbs)/sizeof(imperativeVerbs[0]))) return true;

  // Polite imperative: "please turn..."
  if(
    words[0]=="please" &&
    count>=2 &&
    nluWordEqualsAny(words[1],imperativeVerbs,sizeof(imperativeVerbs)/sizeof(imperativeVerbs[0]))
  ) return true;

  return false;
}


// =====================================================
// PERSONAL STABILITY
// =====================================================

static bool nluHasTransientMarker(String q){
  q=normalizeInput(q);

  const char* const phrases[]={
    "right now","at the moment","this morning","this afternoon",
    "this evening","this week","these days"
  };

  for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++){
    if(nluContainsPhrase(q,phrases[i])) return true;
  }

  const char* const words[]={
    "now","today","tonight","yesterday","tomorrow","currently",
    "recently","lately","temporarily","just"
  };

  for(size_t i=0;i<sizeof(words)/sizeof(words[0]);i++){
    if(nluContainsToken(q,words[i])) return true;
  }

  return false;
}


static bool nluStableFirstPersonStructure(String q){
  q=normalizeInput(q);

  // Common temporary-state complements keep "I am in ..." from being
  // mistaken for durable profile data. This is a safety guard, not a
  // subject vocabulary list.
  const char* const temporaryStates[]={
    "i am in pain",
    "i am in trouble",
    "i am in danger",
    "i am in shock",
    "i am in a hurry",
    "i am in a bad mood",
    "i am in a good mood"
  };

  for(size_t i=0;i<sizeof(temporaryStates)/sizeof(temporaryStates[0]);i++){
    if(q.startsWith(temporaryStates[i])) return false;
  }

  // Possessive profile facts are open-vocabulary:
  // "my favorite subject is physics"
  // "my school is ..."
  // "my goal is ..."
  if(
    q.startsWith("my ") &&
    (
      q.indexOf(" is ")>=0 ||
      q.indexOf(" are ")>=0 ||
      q.indexOf(" was ")>=0 ||
      q.indexOf(" were ")>=0
    )
  ) return true;

  // Relation form with arbitrary nouns/names:
  // "x is my ...", "x and y are my ..."
  if(
    q.indexOf(" is my ")>=0 ||
    q.indexOf(" are my ")>=0 ||
    q.indexOf(" was my ")>=0 ||
    q.indexOf(" were my ")>=0
  ) return true;

  // Stable/preference/activity verbs. The complement stays open-vocabulary.
  const char* const starts[]={
    "i like ","i love ","i prefer ","i enjoy ","i dislike ","i hate ",
    "i do not like ","i do not love ","i do not enjoy ","i do not prefer ",
    "i cannot stand ","i can not stand ","i prefer not to ",
    "i study ","i attend ","i work ","i live ","i stay in ",
    "i play ","i use ","i own ","i speak ","i learn ",
    "i have a ","i have an ","i have two ","i have three ",
    "i go to ","i belong to ","i come from ","i am from ",
    "i am in ","i am at ","i am a ","i am an ",
    "i was born ","i grew up ",
    "we live ","we study ","we work ","we have ","we use "
  };

  for(size_t i=0;i<sizeof(starts)/sizeof(starts[0]);i++){
    if(q.startsWith(starts[i])) return true;
  }

  if(
    q.startsWith("i am ") &&
    q.indexOf(" years old")>=0
  ) return true;

  return false;
}


// =====================================================
// FRAME ANALYZER
// =====================================================

ElliNLUFrame elliAnalyzeNLU(String input){

  ElliNLUFrame frame;

  frame.normalized=
    normalizeInput(input);

  frame.normalized.trim();

  if(!frame.normalized.length()){
    return frame;
  }

  String words[28];
  int count=
    nluSplit(
      frame.normalized,
      words,
      28
    );

  frame.wordCount=
    (uint8_t)min(count,255);

  if(count){
    frame.firstWord=words[0];
  }

  bool hasVerb=false;
  bool hasBe=false;
  bool hasHave=false;
  bool hasBeen=false;
  bool hasIng=false;
  bool pastSignal=false;
  bool futureSignal=false;

  for(int i=0;i<count;i++){

    const String& w=words[i];

    if(nluIsFirstPerson(w)) frame.firstPerson=true;
    if(nluIsSecondPerson(w)) frame.secondPerson=true;
    if(nluIsThirdPerson(w)) frame.thirdPerson=true;
    if(nluIsNegation(w)) frame.negated=true;
    if(nluIsModal(w)) frame.modal=true;
    if(nluLooksLikeVerbForm(w)) hasVerb=true;

    if(
      w=="am" || w=="is" || w=="are" ||
      w=="was" || w=="were" ||
      w=="be" || w=="been" || w=="being"
    ) hasBe=true;

    if(w=="have" || w=="has" || w=="had") hasHave=true;
    if(w=="been") hasBeen=true;
    if(w.length()>4 && w.endsWith("ing")) hasIng=true;

    if(
      w=="was" || w=="were" || w=="did" || w=="had" ||
      w=="went" || w=="came" || w=="got" || w=="made" ||
      w=="said" || w=="told" || w=="felt" || w=="thought" ||
      (w.length()>3 && w.endsWith("ed"))
    ) pastSignal=true;

    if(w=="will" || w=="shall") futureSignal=true;

    if(
      i+1<count &&
      w=="going" &&
      words[i+1]=="to"
    ) futureSignal=true;

    if(nluIsConjunction(w)){
      frame.compound=true;
      if(!frame.conjunction.length()) frame.conjunction=w;
    }
  }

  frame.personal=frame.firstPerson;

  frame.question=
    nluGeneralQuestion(
      frame.normalized,
      words,
      count
    );

  frame.command=
    !frame.question &&
    nluGeneralCommand(
      frame.normalized,
      words,
      count
    );

  frame.statement=
    !frame.question &&
    !frame.command &&
    hasVerb;

  frame.fragment=
    !frame.question &&
    !frame.command &&
    !frame.statement;

  if(frame.question) frame.kind=ELLI_SENTENCE_QUESTION;
  else if(frame.command) frame.kind=ELLI_SENTENCE_COMMAND;
  else if(frame.statement) frame.kind=ELLI_SENTENCE_STATEMENT;
  else frame.kind=ELLI_SENTENCE_FRAGMENT;

  // Tense.
  if(futureSignal){
    frame.tense=ELLI_TENSE_FUTURE;
  }
  else if(pastSignal){
    frame.tense=ELLI_TENSE_PAST;
  }
  else if(hasVerb){
    frame.tense=ELLI_TENSE_PRESENT;
  }

  // Aspect.
  if(hasHave && hasBeen && hasIng){
    frame.aspect=ELLI_ASPECT_PERFECT_CONTINUOUS;
  }
  else if(hasHave){
    frame.aspect=ELLI_ASPECT_PERFECT;
  }
  else if(hasBe && hasIng){
    frame.aspect=ELLI_ASPECT_CONTINUOUS;
  }
  else{
    frame.aspect=ELLI_ASPECT_SIMPLE;
  }

  if(
    frame.personal &&
    frame.statement
  ){

    frame.transientPersonal=
      nluHasTransientMarker(
        frame.normalized
      );

    frame.stablePersonal=
      !frame.transientPersonal &&
      nluStableFirstPersonStructure(
        frame.normalized
      );
  }

  int confidence=55;
  if(frame.question || frame.command || frame.statement) confidence+=18;
  if(frame.personal) confidence+=7;
  if(frame.modal || frame.compound || frame.negated) confidence+=3;
  if(frame.stablePersonal) confidence+=10;
  frame.confidence=(uint8_t)min(confidence,98);

  return frame;
}


bool elliLooksLikeQuestionSyntax(String input){
  return elliAnalyzeNLU(input).question;
}


bool elliLooksLikeCommandSyntax(String input){
  return elliAnalyzeNLU(input).command;
}


bool elliLooksLikePersonalStatementSyntax(String input){
  ElliNLUFrame f=elliAnalyzeNLU(input);
  return f.personal && f.statement;
}


bool elliLooksLikeStablePersonalFactSyntax(String input){
  return elliAnalyzeNLU(input).stablePersonal;
}


bool elliLooksLikeTransientPersonalStatementSyntax(String input){
  ElliNLUFrame f=elliAnalyzeNLU(input);
  return f.personal && f.statement && !f.stablePersonal;
}


// =====================================================
// PERSPECTIVE / SENTENCE FORMING
// =====================================================

static bool nluWordChar(char c){
  return isalnum((unsigned char)c) || c=='_';
}


static void nluReplaceBounded(
  String& text,
  const String& from,
  const String& to
){
  if(!from.length()) return;

  int start=0;

  while(start<text.length()){
    int pos=text.indexOf(from,start);
    if(pos<0) break;

    int after=pos+from.length();

    bool beforeOK=
      pos==0 ||
      !nluWordChar(text[pos-1]);

    bool afterOK=
      after>=text.length() ||
      !nluWordChar(text[after]);

    if(beforeOK && afterOK){
      text=
        text.substring(0,pos)+
        to+
        text.substring(after);
      start=pos+to.length();
    }
    else{
      start=pos+1;
    }
  }
}


static String nluSentenceCase(String s){
  s.trim();

  if(s.length()){
    s.setCharAt(
      0,
      toupper((unsigned char)s[0])
    );
  }

  return s;
}


static String nluContractSecondPerson(String s){

  // Positive auxiliaries.
  if(s.startsWith("you are ")){
    s="you're "+s.substring(8);
  }
  else if(s=="you are"){
    s="you're";
  }
  else if(s.startsWith("you have ")){
    s="you've "+s.substring(9);
  }
  else if(s.startsWith("you will ")){
    s="you'll "+s.substring(9);
  }
  else if(s.startsWith("you would ")){
    s="you'd "+s.substring(10);
  }


  // Natural negative contractions.
  nluReplaceBounded(
    s,
    "you do not",
    "you don't"
  );

  nluReplaceBounded(
    s,
    "you did not",
    "you didn't"
  );

  nluReplaceBounded(
    s,
    "you are not",
    "you aren't"
  );

  nluReplaceBounded(
    s,
    "you were not",
    "you weren't"
  );

  nluReplaceBounded(
    s,
    "you have not",
    "you haven't"
  );

  nluReplaceBounded(
    s,
    "you cannot",
    "you can't"
  );

  nluReplaceBounded(
    s,
    "you can not",
    "you can't"
  );

  nluReplaceBounded(
    s,
    "you should not",
    "you shouldn't"
  );

  nluReplaceBounded(
    s,
    "you would not",
    "you wouldn't"
  );

  nluReplaceBounded(
    s,
    "you will not",
    "you won't"
  );


  return s;
}


static String nluStripFinalPunctuation(String s){

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


  return s;
}


static String nluEnsureSentence(String s){

  s=
    nluStripFinalPunctuation(
      s
    );


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


static bool nluContainsAny(
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

    String needle=
      " "+
      String(values[i])+
      " ";


    if(
      padded.indexOf(
        needle
      )>=0
    ){

      return true;
    }
  }


  return false;
}


static uint8_t nluPickDifferent(
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


static String nluVariant(
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
        nluPickDifferent(
          (uint8_t)count,
          last
        )
      ]
    );
}


String elliPerspectiveToUser(String input){

  String s=
    normalizeInput(
      input
    );


  s.trim();


  // Phrase-level repairs first.
  nluReplaceBounded(s,"i am","you are");
  nluReplaceBounded(s,"i was","you were");
  nluReplaceBounded(s,"i have","you have");
  nluReplaceBounded(s,"i had","you had");
  nluReplaceBounded(s,"i will","you will");
  nluReplaceBounded(s,"i would","you would");
  nluReplaceBounded(s,"i can","you can");
  nluReplaceBounded(s,"i could","you could");
  nluReplaceBounded(s,"i should","you should");
  nluReplaceBounded(s,"i do","you do");
  nluReplaceBounded(s,"i did","you did");

  // Possessive/object pronouns.
  nluReplaceBounded(s,"myself","yourself");
  nluReplaceBounded(s,"mine","yours");
  nluReplaceBounded(s,"my","your");
  nluReplaceBounded(s,"me","you");
  nluReplaceBounded(s,"i","you");

  // Defensive agreement repair.
  nluReplaceBounded(s,"you am","you are");
  nluReplaceBounded(s,"you was","you were");

  s=
    nluContractSecondPerson(
      s
    );


  s=
    nluSentenceCase(
      s
    );


  return s;
}


// =====================================================
// NATURAL RESPONSE COMPOSER
// =====================================================
//
// This is intentionally response-CLASS based.
//
// It does NOT contain:
//   if(input=="i am tired today") ...
//
// Instead it combines:
//   sentence structure
//   tense/aspect
//   transient-vs-stable signal
//   preference polarity
//   broad affect tone
//   conjunctions
//
// Unknown vocabulary still gets a grammatical response.
// =====================================================

String elliNaturalPersonalAcknowledgement(String input){

  String q=
    normalizeInput(
      input
    );


  ElliNLUFrame f=
    elliAnalyzeNLU(
      q
    );


  String natural=
    nluEnsureSentence(
      elliPerspectiveToUser(
        q
      )
    );


  static uint8_t lastLowEnergy=255;
  static uint8_t lastNegative=255;
  static uint8_t lastPositive=255;
  static uint8_t lastPreference=255;
  static uint8_t lastCompound=255;
  static uint8_t lastGeneral=255;


  // ---------------------------------------------------
  // NEGATIVE PREFERENCES
  // ---------------------------------------------------

  bool negativePreference=

    q.indexOf("i do not like ")>=0 ||
    q.indexOf("i do not enjoy ")>=0 ||
    q.indexOf("i dislike ")>=0 ||
    q.indexOf("i hate ")>=0 ||
    q.indexOf("i cannot stand ")>=0 ||
    q.indexOf("i can not stand ")>=0 ||
    q.indexOf("i prefer not to ")>=0;


  if(negativePreference){

    const char* const a[]={

      "Fair enough — ",
      "Makes sense — ",
      "I get that — ",
      "Yeah, I can see that — ",
      "That's understandable — "
    };


    return
      nluVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastPreference
      )
      +
      natural;
  }


  // ---------------------------------------------------
  // POSITIVE PREFERENCES
  // ---------------------------------------------------

  bool positivePreference=

    q.indexOf("i like ")>=0 ||
    q.indexOf("i love ")>=0 ||
    q.indexOf("i enjoy ")>=0 ||
    q.indexOf("i prefer ")>=0;


  if(positivePreference){

    const char* const a[]={

      "Nice — ",
      "That sounds like your kind of thing — ",
      "I can see why that appeals to you — ",
      "Cool — ",
      "That tracks — "
    };


    return
      nluVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastPreference
      )
      +
      natural;
  }


  // ---------------------------------------------------
  // TRANSIENT LOW-ENERGY STATE
  // ---------------------------------------------------

  const char* const lowEnergyWords[]={

    "tired",
    "exhausted",
    "sleepy",
    "drained",
    "fatigued",
    "worn out",
    "low energy"
  };


  if(
    f.transientPersonal &&
    nluContainsAny(
      q,
      lowEnergyWords,
      sizeof(lowEnergyWords)/
      sizeof(lowEnergyWords[0])
    )
  ){

    const char* const replies[]={

      "Sounds like today has taken a lot out of you. Hope you get a chance to recharge a bit.",
      "You sound pretty worn out today. Want to tell me what made the day so tiring?",
      "That sounds exhausting. A little downtime might feel good.",
      "Seems like your energy is running low today. Hope the rest of the day is easier on you.",
      "Oof, sounds like it's been a tiring one. What wore you out most?"
    };


    return
      nluVariant(
        replies,
        sizeof(replies)/sizeof(replies[0]),
        lastLowEnergy
      );
  }


  // ---------------------------------------------------
  // BROAD DIFFICULT / NEGATIVE STATE
  // ---------------------------------------------------

  const char* const negativeWords[]={

    "stressed",
    "worried",
    "anxious",
    "frustrated",
    "upset",
    "sad",
    "angry",
    "annoyed",
    "overwhelmed",
    "nervous",
    "lonely",
    "bored",
    "rough",
    "difficult"
  };


  if(
    f.transientPersonal &&
    nluContainsAny(
      q,
      negativeWords,
      sizeof(negativeWords)/
      sizeof(negativeWords[0])
    )
  ){

    const char* const replies[]={

      "That sounds rough. Want to tell me what happened?",
      "I hear you. Sounds like there's a lot going on there.",
      "Yeah, that doesn't sound like an easy moment. Want to talk it through?",
      "That sounds frustrating. What's been the hardest part?",
      "I get you. If you want, tell me a bit more about what made it feel that way."
    };


    return
      nluVariant(
        replies,
        sizeof(replies)/sizeof(replies[0]),
        lastNegative
      );
  }


  // ---------------------------------------------------
  // BROAD POSITIVE STATE
  // ---------------------------------------------------

  const char* const positiveWords[]={

    "happy",
    "excited",
    "great",
    "good",
    "awesome",
    "proud",
    "relaxed",
    "calm",
    "energetic",
    "amazing",
    "fun"
  };


  if(
    f.transientPersonal &&
    nluContainsAny(
      q,
      positiveWords,
      sizeof(positiveWords)/
      sizeof(positiveWords[0])
    )
  ){

    const char* const replies[]={

      "Nice! Sounds like things are going pretty well.",
      "Love that energy — sounds like you're having a good one.",
      "That's good to hear! What made it feel that way?",
      "Niceee — sounds like something went right today.",
      "Glad to hear it. What's been the best part?"
    };


    return
      nluVariant(
        replies,
        sizeof(replies)/sizeof(replies[0]),
        lastPositive
      );
  }


  // ---------------------------------------------------
  // CAUSAL / COMPOUND STATEMENT
  // ---------------------------------------------------

  if(
    f.compound &&
    f.conjunction=="because"
  ){

    const char* const a[]={

      "That makes sense — ",
      "I can see the connection there — ",
      "That fits together pretty naturally — ",
      "Yeah, the reason tracks — ",
      "I follow you — "
    };


    return
      nluVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastCompound
      )
      +
      natural;
  }


  // ---------------------------------------------------
  // PERFECT / CONTINUOUS ACTIVITY
  // ---------------------------------------------------

  if(
    f.aspect==
      ELLI_ASPECT_PERFECT_CONTINUOUS ||

    f.aspect==
      ELLI_ASPECT_CONTINUOUS
  ){

    const char* const a[]={

      "Sounds like you've been pretty involved in that — ",
      "Seems like that's been keeping you busy — ",
      "I can tell that's something you've been spending time on — ",
      "Sounds like that's been part of your day lately — ",
      "Yeah, I follow — "
    };


    return
      nluVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastGeneral
      )
      +
      natural;
  }


  // ---------------------------------------------------
  // GENERAL TRANSIENT PERSONAL STATEMENT
  // ---------------------------------------------------

  if(
    f.transientPersonal
  ){

    const char* const a[]={

      "I hear you — ",
      "Sounds like that's where things are at today — ",
      "Okay, I get the picture — ",
      "Yeah, I follow — ",
      "Makes sense — "
    };


    return
      nluVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastGeneral
      )
      +
      natural;
  }


  // ---------------------------------------------------
  // GENERAL FALLBACK
  // ---------------------------------------------------

  const char* const a[]={

    "Makes sense — ",
    "I hear you — ",
    "Alright — ",
    "Yeah, I follow — ",
    "Okay, that tracks — ",
    "Fair enough — "
  };


  return
    nluVariant(
      a,
      sizeof(a)/sizeof(a[0]),
      lastGeneral
    )
    +
    natural;
}


String elliNaturalStatementAcknowledgement(String input){

  ElliNLUFrame f=
    elliAnalyzeNLU(
      input
    );


  if(f.personal){

    return
      elliNaturalPersonalAcknowledgement(
        input
      );
  }


  static uint8_t lastNeutral=255;


  if(
    f.compound &&
    f.conjunction=="because"
  ){

    const char* const a[]={

      "That makes sense — I follow the connection you're making.",
      "Yeah, I can see how those two points connect.",
      "I follow you — the reason and the main point fit together."
    };


    return
      nluVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastNeutral
      );
  }


  if(f.negated){

    const char* const a[]={

      "Understood — I follow what you're saying.",
      "Makes sense — I caught the distinction.",
      "Yep, I follow — you're saying the opposite case does not apply."
    };


    return
      nluVariant(
        a,
        sizeof(a)/sizeof(a[0]),
        lastNeutral
      );
  }


  const char* const a[]={

    "I follow you.",
    "Makes sense.",
    "Okay, I see what you mean.",
    "Yep, I got the point.",
    "Alright, I follow."
  };


  return
    nluVariant(
      a,
      sizeof(a)/sizeof(a[0]),
      lastNeutral
    );
}


// =====================================================
// MEMORY-SPECIFIC NATURAL RESPONSES
// =====================================================

String elliNaturalMemorySavedReply(
  String input,
  bool explicitSave
){

  String natural=
    nluEnsureSentence(
      elliPerspectiveToUser(
        input
      )
    );


  static uint8_t lastExplicit=255;
  static uint8_t lastAuto=255;


  if(explicitSave){

    const char* const a[]={

      "Absolutely — ",
      "Done — ",
      "Sure — ",
      "Yep — ",
      "Alright — "
    };


    const char* const b[]={

      " I'll remember that.",
      " That's saved for later.",
      " I'll keep that in memory.",
      " I've stored that for future conversations.",
      " I'll keep that in mind from now on."
    };


    uint8_t i=
      nluPickDifferent(
        (uint8_t)(
          sizeof(a)/
          sizeof(a[0])
        ),
        lastExplicit
      );


    return
      String(a[i])+
      natural+
      String(
        b[
          i%
          (
            sizeof(b)/
            sizeof(b[0])
          )
        ]
      );
  }


  const char* const a[]={

    "Thanks for telling me — ",
    "Okay — ",
    "Nice, I'll keep that context — ",
    "Understood — ",
    "That helps me know you better — "
  };


  const char* const b[]={

    " I'll keep it in memory.",
    " I'll remember it for later.",
    " That's now part of your local profile.",
    " I'll use that context when it's relevant.",
    " I'll keep that in mind for future questions."
  };


  uint8_t i=
    nluPickDifferent(
      (uint8_t)(
        sizeof(a)/
        sizeof(a[0])
      ),
      lastAuto
    );


  return
    String(a[i])+
    natural+
    String(
      b[
        i%
        (
          sizeof(b)/
          sizeof(b[0])
        )
      ]
    );
}


String elliNaturalMemoryKnownReply(
  String input
){

  String natural=
    nluEnsureSentence(
      elliPerspectiveToUser(
        input
      )
    );


  static uint8_t last=255;


  const char* const a[]={

    "Yep — ",
    "Right — ",
    "I remember — ",
    "Exactly — ",
    "Yes, that's already in my memory — "
  };


  const char* const b[]={

    " I remember.",
    " That's already saved.",
    " I've got that one.",
    " That's part of what I remember about you.",
    " No need to tell me twice."
  };


  uint8_t i=
    nluPickDifferent(
      (uint8_t)(
        sizeof(a)/
        sizeof(a[0])
      ),
      last
    );


  return
    String(a[i])+
    natural+
    String(
      b[
        i%
        (
          sizeof(b)/
          sizeof(b[0])
        )
      ]
    );
}


String elliNaturalMemoryUnsavedReply(
  String input
){

  String natural=
    nluEnsureSentence(
      elliPerspectiveToUser(
        input
      )
    );


  static uint8_t last=255;


  const char* const a[]={

    "Thanks for telling me — ",
    "I hear you — ",
    "Okay — ",
    "Makes sense — ",
    "Alright — "
  };


  const char* const b[]={

    " I understand it, but I won't save it permanently unless you ask.",
    " I can use that in this conversation; say 'remember that...' if you want it stored.",
    " I'm following, but permanent memory stays off unless you ask me to save it.",
    " I'll understand that for now; you can explicitly ask me to remember it later.",
    " I won't silently store it, but I understand what you mean."
  };


  uint8_t i=
    nluPickDifferent(
      (uint8_t)(
        sizeof(a)/
        sizeof(a[0])
      ),
      last
    );


  return
    String(a[i])+
    natural+
    String(
      b[
        i%
        (
          sizeof(b)/
          sizeof(b[0])
        )
      ]
    );
}


const char* elliSentenceKindName(ElliSentenceKind kind){
  switch(kind){
    case ELLI_SENTENCE_QUESTION: return "QUESTION";
    case ELLI_SENTENCE_COMMAND: return "COMMAND";
    case ELLI_SENTENCE_STATEMENT: return "STATEMENT";
    case ELLI_SENTENCE_FRAGMENT: return "FRAGMENT / TOPIC";
    default: return "UNKNOWN";
  }
}


const char* elliTenseName(ElliTense tense){
  switch(tense){
    case ELLI_TENSE_PRESENT: return "PRESENT";
    case ELLI_TENSE_PAST: return "PAST";
    case ELLI_TENSE_FUTURE: return "FUTURE";
    default: return "UNKNOWN";
  }
}


const char* elliAspectName(ElliAspect aspect){
  switch(aspect){
    case ELLI_ASPECT_CONTINUOUS: return "CONTINUOUS";
    case ELLI_ASPECT_PERFECT: return "PERFECT";
    case ELLI_ASPECT_PERFECT_CONTINUOUS: return "PERFECT CONTINUOUS";
    default: return "SIMPLE";
  }
}


void printElliNLUFrame(String input){
  ElliNLUFrame f=elliAnalyzeNLU(input);

  Serial.println();
  Serial.println("========== ELLI UNIVERSAL NLU ==========");

  Serial.print("Normalized : "); Serial.println(f.normalized);
  Serial.print("Kind       : "); Serial.println(elliSentenceKindName(f.kind));
  Serial.print("Tense      : "); Serial.println(elliTenseName(f.tense));
  Serial.print("Aspect     : "); Serial.println(elliAspectName(f.aspect));
  Serial.print("Personal   : "); Serial.println(f.personal ? "YES" : "NO");
  Serial.print("Stable     : "); Serial.println(f.stablePersonal ? "YES" : "NO");
  Serial.print("Negated    : "); Serial.println(f.negated ? "YES" : "NO");
  Serial.print("Modal      : "); Serial.println(f.modal ? "YES" : "NO");
  Serial.print("Compound   : "); Serial.println(f.compound ? "YES" : "NO");

  if(f.conjunction.length()){
    Serial.print("Connector  : "); Serial.println(f.conjunction);
  }

  Serial.print("Confidence : "); Serial.println(f.confidence);
  Serial.println("=========================================");
}
