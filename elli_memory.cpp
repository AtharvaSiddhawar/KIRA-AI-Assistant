#include <Arduino.h>
#include <SD.h>
#include "elli_memory.h"
#include "kira_storage.h"
#include "elli_nlu.h"
#include "elli_semantic_v2.h"
#include "elli_response_composer.h"

// Helpers still in kira_brain.ino during modular migration.
String normalizeInput(String s);
void elliSay(const String& s);
String buildVariation(
  const char* const A[], size_t na,
  const char* const B[], size_t nb,
  const char* const C[], size_t nc,
  uint32_t &last
);

static uint32_t lastMemoryAckSig=0xFFFFFFFF;
static const char* MEMORY_DB_PATH="/elli_brain/memory_db.tsv";

const char* const MEMORY_ACK_A[]={
  "Got it. ","Okay. ","Understood. ","I'll keep that in mind. ",
  "Saved. ","Alright. ","I can remember that. ","That's stored. ",
  "I've got it. ","Noted for KIRA. "
};

const char* const MEMORY_ACK_B[]={
  "I'll use that when it helps our conversations",
  "I'll keep it as part of your local profile",
  "I'll remember it instead of treating it like a web question",
  "that can help me understand your future requests",
  "I'll keep that information locally",
  "I'll connect it with your future questions when useful",
  "that is now part of what I know about you",
  "I'll use it carefully when it is relevant",
  "I can refer back to it later",
  "that gives me a little more context about you"
};

const char* const MEMORY_ACK_C[]={
  ".","!"," You can ask what I remember anytime.",
  " If it changes, just tell me."," You can also tell me to forget it.",
  " I'll keep the response natural."," I won't search the web for that.",
  " That should make future conversations smoother."
};


static String sanitizeDbField(String s){
  s.replace("\t"," ");
  s.replace("\r"," ");
  s.replace("\n"," ");
  while(s.indexOf("  ")>=0) s.replace("  "," ");
  s.trim();
  return s;
}


static bool memoryStopWord(const String& w){
  const char* words[]={
    "what","when","where","why","how","who","which","do","does","did",
    "you","remember","tell","told","know","about","me","my","i","am",
    "is","are","was","were","the","a","an","to","of","at","on","in",
    "for","and","that","this","have","has","called"
  };

  for(size_t i=0;i<sizeof(words)/sizeof(words[0]);i++){
    if(w==words[i]) return true;
  }
  return false;
}


static bool sdMemoryContainsExact(String text);


// =====================================================
//        PERSONAL LANGUAGE / SEMANTIC VOCABULARY
// =====================================================
//
// Memory should understand MEANING, not only exact wording.
//
// Examples handled as one semantic family:
//
//   sister / sisters
//   brother / brothers
//   sibling / siblings
//
//   mom / mum / mother
//   dad / papa / father
//
//   favourite / favorite
//
// This is vocabulary-level normalization, NOT hard-coded facts.
// =====================================================

static String memoryCanonicalWord(String w){

  w=normalizeInput(w);
  w.trim();


  if(
    w=="favourite" ||
    w=="favourites" ||
    w=="favorites"
  ){
    return "favorite";
  }


  if(
    w=="sisters"
  ){
    return "sister";
  }


  if(
    w=="brothers"
  ){
    return "brother";
  }


  if(
    w=="siblings"
  ){
    return "sibling";
  }


  if(
    w=="moms" ||
    w=="mom" ||
    w=="mums" ||
    w=="mum" ||
    w=="mama" ||
    w=="mamma"
  ){
    return "mother";
  }


  if(
    w=="dads" ||
    w=="dad" ||
    w=="papa" ||
    w=="daddy"
  ){
    return "father";
  }


  if(
    w=="parents"
  ){
    return "parent";
  }


  if(
    w=="friends"
  ){
    return "friend";
  }


  if(
    w=="cousins"
  ){
    return "cousin";
  }


  if(
    w=="uncles"
  ){
    return "uncle";
  }


  if(
    w=="aunts"
  ){
    return "aunt";
  }


  if(
    w=="grandparents"
  ){
    return "grandparent";
  }


  if(
    w=="grandmothers" ||
    w=="grandma" ||
    w=="grandmom" ||
    w=="granny"
  ){
    return "grandmother";
  }


  if(
    w=="grandfathers" ||
    w=="grandpa" ||
    w=="granddad"
  ){
    return "grandfather";
  }


  // Open-vocabulary profile concept aliases. These are semantic
  // families, not question-specific patches.
  if(
    w=="grade" ||
    w=="grades" ||
    w=="standard" ||
    w=="standards" ||
    w=="classes"
  ){
    return "class";
  }

  if(
    w=="schooling" ||
    w=="education" ||
    w=="studies" ||
    w=="studying"
  ){
    return "study";
  }

  if(
    w=="job" ||
    w=="occupation" ||
    w=="profession"
  ){
    return "work";
  }

  if(
    w=="reside" ||
    w=="resides" ||
    w=="residing"
  ){
    return "live";
  }

  if(
    w=="likes" ||
    w=="liked" ||
    w=="loving" ||
    w=="loves" ||
    w=="enjoy" ||
    w=="enjoys" ||
    w=="enjoying"
  ){
    return "like";
  }

  if(
    w=="teachers"
  ){
    return "teacher";
  }


  if(
    w=="pets"
  ){
    return "pet";
  }


  if(
    w=="hobbies"
  ){
    return "hobby";
  }


  if(
    w=="projects"
  ){
    return "project";
  }


  if(
    w=="classes" ||
    w=="grades"
  ){
    return "class";
  }


  // Conservative plural normalization for longer ordinary nouns.
  //
  // We intentionally avoid aggressive stemming because a memory
  // system must not merge unrelated words.
  if(
    w.length()>5 &&
    w.endsWith("ies")
  ){
    return
      w.substring(
        0,
        w.length()-3
      )+
      "y";
  }


  if(
    w.length()>4 &&
    w.endsWith("s") &&
    !w.endsWith("ss")
  ){
    return
      w.substring(
        0,
        w.length()-1
      );
  }


  return w;
}


static bool memoryHasCanonicalToken(
  String text,
  const String& canonical
){

  text=normalizeInput(text);

  int pos=0;


  while(pos<text.length()){

    while(
      pos<text.length() &&
      text[pos]==' '
    ){
      pos++;
    }


    if(pos>=text.length()){
      break;
    }


    int end=
      text.indexOf(
        ' ',
        pos
      );


    if(end<0){
      end=text.length();
    }


    String w=
      memoryCanonicalWord(
        text.substring(
          pos,
          end
        )
      );


    if(w==canonical){
      return true;
    }


    pos=
      end+1;
  }


  return false;
}


static bool memoryIsKinshipWord(
  String word
){

  word=
    memoryCanonicalWord(
      word
    );


  const char* const relations[]={

    "sister",
    "brother",
    "sibling",

    "mother",
    "father",
    "parent",

    "cousin",
    "uncle",
    "aunt",

    "grandmother",
    "grandfather",
    "grandparent"
  };


  for(
    size_t i=0;
    i<
    sizeof(relations)/
    sizeof(relations[0]);
    i++
  ){

    if(
      word==
      relations[i]
    ){

      return true;
    }
  }


  return false;
}


static bool memoryContainsKinship(
  String text
){

  text=
    normalizeInput(
      text
    );


  int pos=0;


  while(pos<text.length()){

    while(
      pos<text.length() &&
      text[pos]==' '
    ){
      pos++;
    }


    if(pos>=text.length()){
      break;
    }


    int end=
      text.indexOf(
        ' ',
        pos
      );


    if(end<0){
      end=text.length();
    }


    if(
      memoryIsKinshipWord(
        text.substring(
          pos,
          end
        )
      )
    ){

      return true;
    }


    pos=
      end+1;
  }


  return false;
}


static bool memorySemanticTokenMatch(
  const String& queryWord,
  String text
){

  String q=
    memoryCanonicalWord(
      queryWord
    );


  if(!q.length()){
    return false;
  }


  // Exact semantic token match.
  if(
    memoryHasCanonicalToken(
      text,
      q
    )
  ){

    return true;
  }


  // "siblings" is a useful umbrella request.
  //
  // A user asking "who are my siblings?" should be able to
  // retrieve memories stored as "my sister..." or "my brother...".
  if(
    q=="sibling"
  ){

    return

      memoryHasCanonicalToken(
        text,
        "sister"
      )

      ||

      memoryHasCanonicalToken(
        text,
        "brother"
      );
  }


  // A gender-specific relation can safely match a generic
  // "sibling" memory, but never the opposite gender.
  if(
    q=="sister"
  ){

    return
      memoryHasCanonicalToken(
        text,
        "sibling"
      );
  }


  if(
    q=="brother"
  ){

    return
      memoryHasCanonicalToken(
        text,
        "sibling"
      );
  }


  // Parent umbrella.
  if(
    q=="parent"
  ){

    return

      memoryHasCanonicalToken(
        text,
        "mother"
      )

      ||

      memoryHasCanonicalToken(
        text,
        "father"
      );
  }


  return false;
}


// =====================================================
//                 PERSPECTIVE SHIFT
// =====================================================
//
// Stored user facts are normally first-person:
//
//   "sai and jagruti are my sisters"
//
// Elli should reply naturally in second-person:
//
//   "Sai and Jagruti are your sisters."
//
// This keeps memory recall conversational instead of robotic.
// =====================================================

static void memoryReplacePhrase(
  String& text,
  const String& from,
  const String& to
){

  String padded=
    " "+
    text+
    " ";


  String needle=
    " "+
    from+
    " ";


  String replacement=
    " "+
    to+
    " ";


  int pos=0;


  while(
    (
      pos=
      padded.indexOf(
        needle,
        pos
      )
    )>=0
  ){

    padded=

      padded.substring(
        0,
        pos
      )

      +

      replacement

      +

      padded.substring(
        pos+
        needle.length()
      );


    pos+=
      replacement.length();
  }


  padded.trim();

  text=padded;
}


static String titleCaseNamePhrase(
  String phrase
){

  phrase.trim();

  String out="";
  int pos=0;


  while(pos<phrase.length()){

    while(
      pos<phrase.length() &&
      phrase[pos]==' '
    ){
      pos++;
    }


    if(pos>=phrase.length()){
      break;
    }


    int end=
      phrase.indexOf(
        ' ',
        pos
      );


    if(end<0){
      end=phrase.length();
    }


    String word=
      phrase.substring(
        pos,
        end
      );


    String lower=word;
    lower.toLowerCase();


    bool connector=

      lower=="and" ||
      lower=="or" ||
      lower=="of" ||
      lower=="the";


    if(
      !connector &&
      word.length()
    ){

      word.setCharAt(
        0,
        toupper(
          (unsigned char)word[0]
        )
      );
    }


    if(out.length()){
      out+=" ";
    }


    out+=word;

    pos=end+1;
  }


  return out;
}


static String prettyPersonalFact(
  String fact
){

  // Universal perspective/grammar layer handles arbitrary first-person
  // profile facts, not just pre-listed family vocabulary.
  fact=
    elliPerspectiveToUser(
      fact
    );


  fact=
    normalizeInput(
      fact
    );


  fact.trim();


  // ---------------------------------------------------
  // Proper-name presentation for family relations.
  //
  // "sai and jagruti are your sisters"
  // -> "Sai and Jagruti are your sisters"
  //
  // "your sister is sai"
  // -> "your sister is Sai"
  // ---------------------------------------------------

  int relationPos=
    fact.indexOf(
      " are your "
    );


  int verbLength=10;


  if(relationPos<0){

    relationPos=
      fact.indexOf(
        " is your "
      );

    verbLength=9;
  }


  if(relationPos>0){

    int relStart=
      relationPos+
      verbLength;


    int relEnd=
      fact.indexOf(
        ' ',
        relStart
      );


    if(relEnd<0){
      relEnd=fact.length();
    }


    String relation=
      fact.substring(
        relStart,
        relEnd
      );


    if(
      memoryIsKinshipWord(
        relation
      )
    ){

      String names=
        titleCaseNamePhrase(
          fact.substring(
            0,
            relationPos
          )
        );


      fact=

        names+

        fact.substring(
          relationPos
        );
    }
  }


  // "your sister is sai"
  int yourPos=
    fact.indexOf(
      "your "
    );


  if(yourPos>=0){

    int relStart=
      yourPos+5;


    int relEnd=
      fact.indexOf(
        ' ',
        relStart
      );


    if(relEnd>relStart){

      String relation=
        fact.substring(
          relStart,
          relEnd
        );


      if(
        memoryIsKinshipWord(
          relation
        )
      ){

        int isPos=
          fact.indexOf(
            " is ",
            relEnd
          );


        int arePos=
          fact.indexOf(
            " are ",
            relEnd
          );


        int nameStart=-1;


        if(isPos>=0){

          nameStart=
            isPos+4;
        }

        else if(arePos>=0){

          nameStart=
            arePos+5;
        }


        if(
          nameStart>=0 &&
          nameStart<fact.length()
        ){

          fact=

            fact.substring(
              0,
              nameStart
            )

            +

            titleCaseNamePhrase(
              fact.substring(
                nameStart
              )
            );
        }
      }
    }
  }


  fact.trim();


  if(fact.length()){

    fact.setCharAt(
      0,
      toupper(
        (unsigned char)fact[0]
      )
    );
  }


  if(
    fact.length() &&
    !fact.endsWith(".") &&
    !fact.endsWith("!") &&
    !fact.endsWith("?")
  ){

    fact+=".";
  }


  return fact;
}


static bool looksLikePersonalRelationStatement(
  String q
){

  q=
    normalizeInput(
      q
    );


  if(
    q.indexOf(" is my ")<0 &&
    q.indexOf(" are my ")<0 &&
    q.indexOf(" was my ")<0 &&
    q.indexOf(" were my ")<0 &&
    !q.startsWith("my ")
  ){

    return false;
  }


  return
    memoryContainsKinship(
      q
    );
}


static bool looksLikeStablePersonalFact(
  String q
){

  q=
    normalizeInput(
      q
    );


  // Universal grammar/stability detector first. This catches open
  // vocabulary such as "i am in 10th grade", "my goal is...",
  // "i live in...", etc.
  if(elliLooksLikeStablePersonalFactSyntax(q)){
    return true;
  }


  // A question is recall, not a new fact.
  const char* const questionStarts[]={

    "what ",
    "who ",
    "where ",
    "when ",
    "why ",
    "how ",
    "which ",

    "is ",
    "are ",
    "was ",
    "were ",

    "do ",
    "does ",
    "did ",

    "can ",
    "could ",
    "would ",
    "should ",

    "tell me ",
    "show me "
  };


  for(
    size_t i=0;
    i<
    sizeof(questionStarts)/
    sizeof(questionStarts[0]);
    i++
  ){

    if(
      q.startsWith(
        questionStarts[i]
      )
    ){

      return false;
    }
  }


  if(
    looksLikePersonalRelationStatement(
      q
    )
  ){

    return true;
  }


  return

    q.startsWith("i like ") ||
    q.startsWith("i love ") ||
    q.startsWith("i prefer ") ||

    q.startsWith("i study ") ||
    q.startsWith("i am in class ") ||
    q.startsWith("i am in grade ") ||
    q.startsWith("i am a student") ||
    q.startsWith("i use ") ||
    q.startsWith("i play ") ||

    q.startsWith("i have a ") ||
    q.startsWith("i have an ") ||

    q.startsWith("my favorite ") ||
    q.startsWith("my favourite ") ||

    q.startsWith("my hobby ") ||
    q.startsWith("my project ");
}


static bool memoryContainsExactAnywhere(
  String text
){

  String normalized=
    normalizeInput(
      text
    );


  for(
    uint8_t i=0;
    i<MAX_BRAIN_MEMORIES;
    i++
  ){

    if(
      !brainStore.memories[i].active
    ){

      continue;
    }


    if(
      normalizeInput(
        String(
          brainStore.memories[i].text
        )
      )
      ==
      normalized
    ){

      return true;
    }
  }


  return
    sdMemoryContainsExact(
      text
    );
}



static String buildKeywords(String text){
  text=normalizeInput(text);
  String out="";
  int pos=0;
  int added=0;

  while(pos<text.length() && added<14){
    while(pos<text.length() && text[pos]==' ') pos++;
    if(pos>=text.length()) break;

    int end=text.indexOf(' ',pos);
    if(end<0) end=text.length();

    String w=text.substring(pos,end);
    w.trim();

    if(w.length()>=3 && !memoryStopWord(w)){
      bool duplicate=false;
      int k=0;
      while(k<out.length()){
        int comma=out.indexOf(',',k);
        if(comma<0) comma=out.length();
        if(out.substring(k,comma)==w){ duplicate=true; break; }
        k=comma+1;
      }

      if(!duplicate){
        if(out.length()) out+=",";
        out+=w;
        added++;
      }
    }

    pos=end+1;
  }

  return out;
}


static int keywordOverlapScore(
  String query,
  String text
){

  query=
    normalizeInput(
      query
    );

  text=
    normalizeInput(
      text
    );


  int score=0;
  int useful=0;
  int pos=0;


  while(pos<query.length()){

    while(
      pos<query.length() &&
      query[pos]==' '
    ){
      pos++;
    }


    if(pos>=query.length()){
      break;
    }


    int end=
      query.indexOf(
        ' ',
        pos
      );


    if(end<0){
      end=query.length();
    }


    String word=
      query.substring(
        pos,
        end
      );


    word.trim();


    if(
      word.length()>=2 &&
      !memoryStopWord(word)
    ){

      useful++;


      if(
        memorySemanticTokenMatch(
          word,
          text
        )
      ){

        score+=12;
      }
    }


    pos=end+1;
  }


  // ---------------------------------------------------
  // Predicate / concept family boosts.
  // ---------------------------------------------------

  if(
    query.indexOf(" like")>=0 &&
    text.indexOf(" like")>=0
  ){
    score+=8;
  }


  if(
    query.indexOf("prefer")>=0 &&
    text.indexOf("prefer")>=0
  ){
    score+=8;
  }


  if(
    (
      query.indexOf("favorite")>=0 ||
      query.indexOf("favourite")>=0
    )

    &&

    (
      text.indexOf("favorite")>=0 ||
      text.indexOf("favourite")>=0
    )
  ){
    score+=10;
  }


  if(
    query.indexOf("project")>=0 &&
    text.indexOf("project")>=0
  ){
    score+=10;
  }


  if(
    query.indexOf("hobby")>=0 &&
    text.indexOf("hobby")>=0
  ){
    score+=10;
  }


  if(
    memoryContainsKinship(query) &&
    memoryContainsKinship(text)
  ){

    score+=8;
  }


  // "who..." strongly prefers memories that contain a person/family
  // relation instead of unrelated facts with one overlapping word.
  if(
    query.startsWith("who ") &&
    memoryContainsKinship(text)
  ){

    score+=7;
  }


  // "where do I study..." / "what school..." family.
  if(
    (
      query.indexOf("study")>=0 ||
      query.indexOf("school")>=0 ||
      query.indexOf("college")>=0 ||
      query.indexOf("university")>=0
    )

    &&

    (
      text.indexOf("study")>=0 ||
      text.indexOf("school")>=0 ||
      text.indexOf("college")>=0 ||
      text.indexOf("university")>=0
    )
  ){

    score+=8;
  }


  if(
    useful==0 &&
    score==0
  ){

    return 0;
  }


  return score;
}


static bool memoryStrongSemanticMatch(
  String query,
  String& best
){

  int bestScore=0;
  best="";


  for(
    uint8_t i=0;
    i<MAX_BRAIN_MEMORIES;
    i++
  ){

    if(
      !brainStore.memories[i].active
    ){

      continue;
    }


    String text=
      String(
        brainStore.memories[i].text
      );


    int score=
      keywordOverlapScore(
        query,
        text
      );


    if(score>bestScore){

      bestScore=score;
      best=text;
    }
  }


  if(
    brainSdReady &&
    SD.exists(
      MEMORY_DB_PATH
    )
  ){

    File f=
      SD.open(
        MEMORY_DB_PATH,
        FILE_READ
      );


    if(f){

      while(f.available()){

        String line=
          f.readStringUntil(
            '\n'
          );


        line.trim();


        if(
          !line.length() ||
          line.startsWith("timestamp\t")
        ){

          continue;
        }


        int p1=line.indexOf('\t');
        int p2=p1<0 ? -1 : line.indexOf('\t',p1+1);
        int p3=p2<0 ? -1 : line.indexOf('\t',p2+1);


        if(p3<0){
          continue;
        }


        String text=
          line.substring(
            p3+1
          );


        int score=
          keywordOverlapScore(
            query,
            text
          );


        if(score>bestScore){

          bestScore=score;
          best=text;
        }
      }


      f.close();
    }
  }


  // A strong relation/predicate match normally scores well
  // above this. The threshold avoids "remembering" an unrelated
  // fact because of one generic overlapping word.
  return

    best.length() &&
    bestScore>=18;
}


bool looksSensitiveForAutomaticMemory(String q){
  q=normalizeInput(q);

  const char* blocked[]={
    "password","passcode","pin number","otp","one time password",
    "api key","secret key","credit card","debit card","cvv",
    "bank account","home address","house address","private address",
    "medical","diagnosis","mental health","health condition",
    "religion","religious belief","sexual orientation","sex life",
    "political party","political ideology","criminal history"
  };

  for(size_t i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++){
    if(q.indexOf(blocked[i])>=0) return true;
  }
  return false;
}


int memoryCount(){
  int count=0;
  for(uint8_t i=0;i<MAX_BRAIN_MEMORIES;i++){
    if(brainStore.memories[i].active) count++;
  }
  return count;
}


static bool sdMemoryContainsExact(String text){
  if(!brainSdReady || !SD.exists(MEMORY_DB_PATH)) return false;

  File f=SD.open(MEMORY_DB_PATH,FILE_READ);
  if(!f) return false;

  String target=normalizeInput(text);
  bool found=false;

  while(f.available()){
    String line=f.readStringUntil('\n');
    line.trim();
    if(!line.length() || line.startsWith("timestamp\t")) continue;

    int p1=line.indexOf('\t');
    int p2=p1<0 ? -1 : line.indexOf('\t',p1+1);
    int p3=p2<0 ? -1 : line.indexOf('\t',p2+1);
    if(p3<0) continue;

    String stored=line.substring(p3+1);
    if(normalizeInput(stored)==target){ found=true; break; }
  }

  f.close();
  return found;
}


static void appendLongTermMemory(String text,uint8_t category){
  if(!brainSdReady) return;

  if(!SD.exists(MEMORY_DB_PATH)){
    File header=SD.open(MEMORY_DB_PATH,FILE_WRITE);
    if(header){
      header.println("timestamp\tcategory\tkeywords\ttext");
      header.close();
    }
  }

  if(sdMemoryContainsExact(text)) return;

  String clean=sanitizeDbField(text);
  String keys=sanitizeDbField(buildKeywords(clean));

  appendBrainSD(
    MEMORY_DB_PATH,
    brainTimestamp()+"\t"+String(category)+"\t"+keys+"\t"+clean
  );
}


void elliMemoryBegin(){
  if(!brainSdReady){
    Serial.println("[MEMORY] SD database unavailable; using 18-slot NVS recall cache.");
    return;
  }

  if(!SD.exists(MEMORY_DB_PATH)){
    File f=SD.open(MEMORY_DB_PATH,FILE_WRITE);
    if(f){
      f.println("timestamp\tcategory\tkeywords\ttext");
      f.close();
    }
  }

  // One-time-style migration: copy current NVS memories into the SD DB.
  // Exact duplicate checking keeps reboot migration idempotent.
  int migrated=0;
  for(uint8_t i=0;i<MAX_BRAIN_MEMORIES;i++){
    if(!brainStore.memories[i].active) continue;
    String text=brainStore.memories[i].text;
    if(!sdMemoryContainsExact(text)){
      appendLongTermMemory(text,brainStore.memories[i].category);
      migrated++;
    }
  }

  Serial.print("[MEMORY] Searchable SD memory ready");
  if(migrated>0){
    Serial.print("; migrated ");
    Serial.print(migrated);
    Serial.print(" cached memories");
  }
  Serial.println(".");
}


bool addPersonalMemory(String text,uint8_t category){
  text.trim();
  if(!text.length()) return false;

  if(looksSensitiveForAutomaticMemory(text)){
    elliSay(
      "I won't automatically save passwords, codes, private addresses, financial secrets, or sensitive health details as profile memory."
    );
    return true;
  }

  String normalized=normalizeInput(text);

  for(uint8_t i=0;i<MAX_BRAIN_MEMORIES;i++){
    if(!brainStore.memories[i].active) continue;
    if(normalizeInput(String(brainStore.memories[i].text))==normalized){
      // It may already be cached but not yet on SD after a previous no-SD boot.
      appendLongTermMemory(text,category);
      elliSay(
        elliComposeMemoryKnownSemantic(
          text
        )
      );
      return true;
    }
  }

  int slot=-1;
  for(uint8_t i=0;i<MAX_BRAIN_MEMORIES;i++){
    if(!brainStore.memories[i].active){ slot=i; break; }
  }

  // NVS is now only the fast 18-record cache. SD keeps the long-term copy.
  // V1.4: if the cache is full, prefer replacing a lower-importance record
  // before an important Memory V3 record. Legacy records have importance 0.
  if(slot<0){
    slot=0;
    for(uint8_t i=1;i<MAX_BRAIN_MEMORIES;i++){
      uint8_t impI=(uint8_t)(brainStore.memories[i].reserved & 0x00FF);
      uint8_t impSlot=(uint8_t)(brainStore.memories[slot].reserved & 0x00FF);

      if(impI<impSlot ||
         (impI==impSlot && brainStore.memories[i].createdDay<brainStore.memories[slot].createdDay)){
        slot=i;
      }
    }
  }

  brainStore.memories[slot].active=1;
  brainStore.memories[slot].category=category;
  brainStore.memories[slot].reserved=0;
  brainStore.memories[slot].createdDay=currentBrainDay();
  copyBrainText(
    brainStore.memories[slot].text,
    sizeof(brainStore.memories[slot].text),
    text
  );

  saveBrainStore();
  appendLongTermMemory(text,category);

  elliSay(
    elliComposeMemorySavedSemantic(
      text,
      category==1
    )
  );


  return true;
}


static String prettyPersonalName(String name){
  name.trim();
  name.toLowerCase();
  bool newWord=true;

  for(int i=0;i<name.length();i++){
    if(name[i]==' '){ newWord=true; continue; }
    if(newWord){
      name.setCharAt(i,toupper((unsigned char)name[i]));
      newWord=false;
    }
  }
  return name;
}


void showPersonalMemory(){

  Serial.println();

  Serial.println(
    "========== ELLI PERSONAL MEMORY =========="
  );


  Serial.print(
    "Owner name : "
  );

  Serial.println(
    strlen(brainStore.ownerName)
    ?
    brainStore.ownerName
    :
    "(not set)"
  );


  Serial.print(
    "Auto remember ordinary facts : "
  );

  Serial.println(
    brainStore.autoRemember
    ?
    "ON"
    :
    "OFF"
  );


  Serial.print(
    "NVS recall cache : "
  );

  Serial.print(
    memoryCount()
  );

  Serial.print(
    " / "
  );

  Serial.println(
    MAX_BRAIN_MEMORIES
  );


  Serial.print(
    "Long-term SD DB : "
  );

  Serial.println(
    brainSdReady
    ?
    "AVAILABLE"
    :
    "NOT MOUNTED"
  );


  int visible=0;

  String first="";
  String second="";
  String third="";


  for(
    uint8_t i=0;
    i<MAX_BRAIN_MEMORIES;
    i++
  ){

    if(
      !brainStore.memories[i].active
    ){

      continue;
    }


    visible++;


    String fact=
      String(
        brainStore.memories[i].text
      );


    Serial.print("  ");
    Serial.print(visible);
    Serial.print(". ");
    Serial.println(fact);


    if(!first.length()){
      first=fact;
    }

    else if(!second.length()){
      second=fact;
    }

    else if(!third.length()){
      third=fact;
    }
  }


  Serial.println(
    "=========================================="
  );


  // Natural spoken summary. Serial still keeps the full list.
  if(visible<=0){

    elliSay(
      "I don't have any personal facts saved in quick recall yet."
    );

    return;
  }


  String answer=

    "I currently have "+

    String(
      visible
    )+

    (
      visible==1
      ?
      " personal fact in quick recall. "
      :
      " personal facts in quick recall. "
    );


  if(first.length()){

    answer+=
      prettyPersonalFact(
        first
      );
  }


  if(second.length()){

    answer+=" ";

    answer+=
      prettyPersonalFact(
        second
      );
  }


  if(third.length()){

    answer+=" ";

    answer+=
      prettyPersonalFact(
        third
      );
  }


  if(visible>3){

    answer+=
      " The full list is in the Serial Monitor.";
  }


  elliSay(
    answer
  );
}


static bool looksLikePersonalRecallQuestion(
  String q
){

  q=
    normalizeInput(
      q
    );


  // ---------------------------------------------------
  // SYSTEM / DEVICE STATUS GUARD
  // ---------------------------------------------------

  if(
    q.indexOf("wifi")>=0 ||
    q.indexOf("ram")>=0 ||
    q.indexOf("heap")>=0 ||
    q.indexOf("system status")>=0 ||
    q.indexOf("ip address")>=0
  ){
    return false;
  }


  if(
    q.startsWith("memory search ") ||
    q.startsWith("search memory ") ||
    q.indexOf("do you remember")>=0 ||
    q.indexOf("did i tell you")>=0 ||
    q.indexOf("what do you know about")>=0
  ){
    return true;
  }


  if(!elliLooksLikeQuestionSyntax(q)){
    return false;
  }


  // "my ..." questions are personal by construction and should never
  // be sent to the public web as though the internet knows the owner.
  if(
    q.startsWith("my ") ||
    q.indexOf(" my ")>=0 ||
    q.indexOf(" mine")>=0
  ){
    return true;
  }


  bool firstPerson=
    q.startsWith("i ") ||
    q.indexOf(" i ")>=0 ||
    q.endsWith(" i") ||
    q.indexOf(" me ")>=0 ||
    q.endsWith(" me");


  if(!firstPerson){
    return false;
  }


  // Advice/capability questions are NOT memory recall.
  //
  //   how can i build this
  //   what should i choose
  //   could i use this sensor
  //
  // Those belong to KIRA's knowledge/reasoning layer.
  if(
    q.indexOf("can i ")>=0 ||
    q.indexOf("could i ")>=0 ||
    q.indexOf("should i ")>=0 ||
    q.indexOf("would i ")>=0 ||
    q.indexOf("may i ")>=0 ||
    q.indexOf("might i ")>=0
  ){
    return false;
  }


  // Open-vocabulary self-fact questions.
  if(
    q.startsWith("what ") ||
    q.startsWith("who ") ||
    q.startsWith("where ") ||
    q.startsWith("when ") ||
    q.startsWith("which ") ||
    q.startsWith("how old ") ||
    q.startsWith("how many ")
  ){
    return true;
  }


  // Yes/no recall predicates with open complements.
  const char* const recallStarts[]={
    "am i ","was i ",
    "do i like ","do i love ","do i prefer ","do i have ",
    "did i tell ","have i told ","have i said ",
    "where did i ","when did i "
  };


  for(
    size_t i=0;
    i<sizeof(recallStarts)/sizeof(recallStarts[0]);
    i++
  ){
    if(q.startsWith(recallStarts[i])){
      return true;
    }
  }


  return false;
}


static String formatRoutineTimeLocal(int hour,int minute){
  char b[20];
  int h12=hour%12;
  if(h12==0) h12=12;
  snprintf(b,sizeof(b),"%d:%02d %s",h12,minute,hour>=12 ? "PM" : "AM");
  return String(b);
}


static bool recallRoutine(String q){
  if(q.indexOf("when")<0 && q.indexOf("what time")<0) return false;

  int best=-1;
  int bestScore=0;

  for(uint8_t i=0;i<MAX_BRAIN_ROUTINES;i++){
    if(!brainStore.routines[i].active) continue;
    int score=keywordOverlapScore(q,String(brainStore.routines[i].title));
    if(score>bestScore){ bestScore=score; best=i; }
  }

  if(best>=0 && bestScore>0){
    elliSay(
      "You have \""+String(brainStore.routines[best].title)+"\" at "+
      formatRoutineTimeLocal(brainStore.routines[best].hour,brainStore.routines[best].minute)+" daily."
    );
    return true;
  }

  return false;
}


static void considerMemoryCandidate(
  const String& q,
  const String& text,
  int& bestScore,
  String& best,
  int& secondScore,
  String& second,
  int& thirdScore,
  String& third
){
  int score=keywordOverlapScore(q,text);
  if(score<=0) return;

  String n=normalizeInput(text);
  if(n==normalizeInput(best) || n==normalizeInput(second) || n==normalizeInput(third)) return;

  if(score>bestScore){
    thirdScore=secondScore; third=second;
    secondScore=bestScore; second=best;
    bestScore=score; best=text;
  }else if(score>secondScore){
    thirdScore=secondScore; third=second;
    secondScore=score; second=text;
  }else if(score>thirdScore){
    thirdScore=score; third=text;
  }
}


static bool searchMemory(
  String q
){

  int bestScore=0;
  int secondScore=0;
  int thirdScore=0;

  String best="";
  String second="";
  String third="";


  // Search the small NVS cache first.
  for(
    uint8_t i=0;
    i<MAX_BRAIN_MEMORIES;
    i++
  ){

    if(
      !brainStore.memories[i].active
    ){
      continue;
    }


    considerMemoryCandidate(

      q,

      String(
        brainStore.memories[i].text
      ),

      bestScore,
      best,

      secondScore,
      second,

      thirdScore,
      third
    );
  }


  // Search the complete SD database line-by-line.
  if(
    brainSdReady &&
    SD.exists(
      MEMORY_DB_PATH
    )
  ){

    File f=
      SD.open(
        MEMORY_DB_PATH,
        FILE_READ
      );


    if(f){

      while(f.available()){

        String line=
          f.readStringUntil(
            '\n'
          );


        line.trim();


        if(
          !line.length() ||
          line.startsWith("timestamp\t")
        ){

          continue;
        }


        int p1=
          line.indexOf('\t');


        int p2=
          p1<0
          ?
          -1
          :
          line.indexOf(
            '\t',
            p1+1
          );


        int p3=
          p2<0
          ?
          -1
          :
          line.indexOf(
            '\t',
            p2+1
          );


        if(p3<0){
          continue;
        }


        String text=
          line.substring(
            p3+1
          );


        considerMemoryCandidate(

          q,
          text,

          bestScore,
          best,

          secondScore,
          second,

          thirdScore,
          third
        );
      }


      f.close();
    }
  }


  // Require a real semantic hit.
  if(
    bestScore<=0 ||
    !best.length()
  ){

    return false;
  }


  // ---------------------------------------------------
  // MULTI-MEMORY ANSWER SELECTION
  // ---------------------------------------------------

  bool broadMemoryQuestion=

    q.indexOf("what do you know about")>=0 ||

    q.indexOf("what did i tell you about")>=0 ||

    q.startsWith("memory search ") ||

    q.startsWith("search memory ");


  bool siblingUmbrella=

    memoryHasCanonicalToken(
      q,
      "sibling"
    );


  bool includeSecond=

    second.length()

    &&

    (
      broadMemoryQuestion ||
      siblingUmbrella
    )

    &&

    secondScore>=
    bestScore-4;


  bool includeThird=

    third.length()

    &&

    broadMemoryQuestion

    &&

    thirdScore>=
    bestScore-4;


  elliSay(
    elliComposeMemoryRecallSemantic(

      q,

      best,

      includeSecond
      ?
      second
      :
      "",

      includeThird
      ?
      third
      :
      ""
    )
  );


  return true;
}


// =====================================================
//        UNIVERSAL PERSONAL STATEMENT CONVERSATION
// =====================================================
//
// Handles arbitrary first-person declarative sentences that are NOT
// stable profile facts or explicit memory commands.
//
// Examples:
//   "i am tired"
//   "i had a long day"
//   "i am working on something difficult"
//
// These are understood locally rather than being treated as malformed
// web questions. They are NOT auto-saved as long-term profile memory.
// =====================================================

bool handleNaturalPersonalStatement(String q){

  q=normalizeInput(q);

  if(!elliLooksLikePersonalStatementSyntax(q)){
    return false;
  }

  // Stable facts belong to the profile-memory handler which runs before
  // this function in kira_brain.ino.
  if(elliLooksLikeStablePersonalFactSyntax(q)){
    return false;
  }

  elliSay(
    elliComposePersonalSemantic(
      q
    )
  );

  return true;
}


bool handlePersonalRecallIntent(String q){
  q=normalizeInput(q);
  if(!looksLikePersonalRecallQuestion(q)) return false;

  if(q.startsWith("memory search ")) q=q.substring(14);
  else if(q.startsWith("search memory ")) q=q.substring(14);

  q.trim();

  if(recallRoutine(q)) return true;
  if(searchMemory(q)) return true;

  elliSay(
    "I checked my personal memory first, but I don't have a matching fact saved yet. I won't send that personal question to the web."
  );
  return true;
}


bool handleProfileMemoryIntent(
  String q
){

  q=
    normalizeInput(
      q
    );


  // ===================================================
  // OWNER NAME
  // ===================================================

  if(
    q=="what is my name" ||
    q=="whats my name" ||
    q=="who am i" ||
    q=="do you know my name"
  ){

    if(
      strlen(
        brainStore.ownerName
      )
    ){

      elliSay(
        "Your name is "+
        String(
          brainStore.ownerName
        )+
        "."
      );
    }

    else{

      elliSay(
        "You haven't told me what name you want me to use yet."
      );
    }


    return true;
  }


  if(
    q.startsWith(
      "my name is "
    )
  ){

    String name=
      prettyPersonalName(
        q.substring(
          11
        )
      );


    if(name.length()){

      copyBrainText(
        brainStore.ownerName,
        sizeof(
          brainStore.ownerName
        ),
        name
      );


      saveBrainStore();


      elliSay(
        "Got it — I'll call you "+
        name+
        "."
      );
    }


    return true;
  }


  if(
    q.startsWith(
      "call me "
    )
  ){

    String name=
      prettyPersonalName(
        q.substring(
          8
        )
      );


    if(name.length()){

      copyBrainText(
        brainStore.ownerName,
        sizeof(
          brainStore.ownerName
        ),
        name
      );


      saveBrainStore();


      elliSay(
        "Sure — I'll call you "+
        name+
        "."
      );
    }


    return true;
  }


  // ===================================================
  // MEMORY MODE
  // ===================================================

  if(
    q=="remember everything about me" ||
    q=="remember things about me" ||
    q=="auto remember on"
  ){

    brainStore.autoRemember=1;

    saveBrainStore();


    elliSay(
      "Automatic personal memory is on. I'll remember ordinary stable facts and preferences when they're useful, while still excluding passwords, private addresses, financial secrets, and sensitive health information."
    );


    return true;
  }


  if(
    q=="auto remember off" ||
    q=="stop automatically remembering" ||
    q=="do not automatically remember me"
  ){

    brainStore.autoRemember=0;

    saveBrainStore();


    elliSay(
      "Automatic personal memory is off. You can still save anything appropriate by saying, 'remember that...'."
    );


    return true;
  }


  if(
    q=="what do you remember about me" ||
    q=="show my memories" ||
    q=="show personal memory" ||
    q=="what do you know about me"
  ){

    showPersonalMemory();

    return true;
  }


  // ===================================================
  // EXPLICIT REMEMBER
  // ===================================================

  if(
    q.startsWith(
      "remember that "
    )
  ){

    String fact=
      q.substring(
        14
      );


    fact.trim();


    return
      addPersonalMemory(
        fact,
        1
      );
  }


  if(
    q.startsWith(
      "remember "
    )
  ){

    String fact=
      q.substring(
        9
      );


    fact.trim();


    if(
      !fact.startsWith("to ") &&
      fact.length()
    ){

      return
        addPersonalMemory(
          fact,
          1
        );
    }
  }


  // ===================================================
  // NATURAL PERSONAL FACT STATEMENTS
  // ===================================================
  //
  // Examples:
  //
  //   "Sai and Jagruti are my sisters."
  //   "My favorite game is Minecraft."
  //   "I study at ..."
  //
  // These should NEVER be treated as broken questions or
  // sent to the web.
  //
  // If auto-memory is ON -> save.
  // If already saved -> acknowledge that Elli remembers it.
  // If auto-memory is OFF -> understand it locally, but be
  // honest that it is not being permanently saved.
  // ===================================================

  if(
    looksLikeStablePersonalFact(
      q
    )
  ){

    if(
      looksSensitiveForAutomaticMemory(
        q
      )
    ){

      elliSay(
        "I understand what you said, but I won't save sensitive personal information as profile memory."
      );


      return true;
    }


    String rememberedFact="";


    bool exactMemory=
      memoryContainsExactAnywhere(
        q
      );


    bool semanticMemory=
      exactMemory

      ||

      memoryStrongSemanticMatch(
        q,
        rememberedFact
      );


    if(semanticMemory){

      String factToSay=

        exactMemory

        ?

        q

        :

        rememberedFact;


      elliSay(
        elliComposeMemoryKnownSemantic(
          factToSay
        )
      );


      return true;
    }


    if(
      brainStore.autoRemember
    ){

      return
        addPersonalMemory(
          q,
          2
        );
    }


    elliSay(
      elliComposeMemoryUnsavedSemantic(
        q
      )
    );


    return true;
  }


  return false;
}

// =====================================================
//                  PERSONAL MEMORY V2
// =====================================================
// Adds a RAM-only session layer and explicit forgetting without changing the
// existing BrainStore binary layout. That keeps all old NVS data compatible.
// =====================================================

namespace {

static const uint8_t MAX_SESSION_MEMORIES = 10;
String sessionMemories[MAX_SESSION_MEMORIES];
uint8_t sessionMemoryUsed = 0;

static String memoryV2CleanFact(String s){
  s.trim();
  while(s.indexOf("  ") >= 0) s.replace("  ", " ");
  return s;
}

static bool memoryV2Matches(const String& storedRaw, const String& needleRaw){
  String stored = normalizeInput(storedRaw);
  String needle = normalizeInput(needleRaw);
  stored.trim();
  needle.trim();
  if(!stored.length() || !needle.length()) return false;
  if(stored == needle) return true;
  if(needle.length() < 4) return false;
  return stored.indexOf(needle) >= 0;
}

static bool addSessionMemoryInternal(String fact){
  fact = memoryV2CleanFact(fact);
  if(!fact.length()) return false;

  if(looksSensitiveForAutomaticMemory(fact)){
    elliSay("I won't put secrets, private addresses, financial details, or sensitive health information into session memory.");
    return true;
  }

  String normalized = normalizeInput(fact);
  for(uint8_t i = 0; i < sessionMemoryUsed; i++){
    if(normalizeInput(sessionMemories[i]) == normalized){
      elliSay("I already have that in this conversation's temporary memory.");
      return true;
    }
  }

  if(sessionMemoryUsed < MAX_SESSION_MEMORIES){
    sessionMemories[sessionMemoryUsed++] = fact;
  }else{
    for(uint8_t i = 1; i < MAX_SESSION_MEMORIES; i++){
      sessionMemories[i - 1] = sessionMemories[i];
    }
    sessionMemories[MAX_SESSION_MEMORIES - 1] = fact;
  }

  elliSay("Okay — I'll remember that for this conversation only. It won't be written to permanent memory.");
  return true;
}

static int forgetSessionMatches(const String& needle){
  int removed = 0;
  uint8_t write = 0;
  for(uint8_t i = 0; i < sessionMemoryUsed; i++){
    if(memoryV2Matches(sessionMemories[i], needle)){
      removed++;
      continue;
    }
    if(write != i) sessionMemories[write] = sessionMemories[i];
    write++;
  }
  for(uint8_t i = write; i < sessionMemoryUsed; i++) sessionMemories[i] = "";
  sessionMemoryUsed = write;
  return removed;
}

static int forgetNvsMatches(const String& needle){
  int removed = 0;
  for(uint8_t i = 0; i < MAX_BRAIN_MEMORIES; i++){
    if(!brainStore.memories[i].active) continue;
    if(!memoryV2Matches(String(brainStore.memories[i].text), needle)) continue;

    brainStore.memories[i].active = 0;
    brainStore.memories[i].category = 0;
    brainStore.memories[i].reserved = 0;
    brainStore.memories[i].createdDay = 0;
    brainStore.memories[i].text[0] = '\0';
    removed++;
  }
  if(removed) saveBrainStore();
  return removed;
}

static int forgetSdMatches(const String& needle){
  if(!brainSdReady || !SD.exists(MEMORY_DB_PATH)) return 0;

  const char* TEMP_PATH = "/elli_brain/memory_db.tmp";
  SD.remove(TEMP_PATH);

  File input = SD.open(MEMORY_DB_PATH, FILE_READ);
  if(!input) return 0;

  File output = SD.open(TEMP_PATH, FILE_WRITE);
  if(!output){
    input.close();
    return 0;
  }

  int removed = 0;
  while(input.available()){
    String line = input.readStringUntil('\n');
    line.trim();

    if(!line.length()) continue;
    if(line.startsWith("timestamp\t")){
      output.println(line);
      continue;
    }

    int p1 = line.indexOf('\t');
    int p2 = p1 < 0 ? -1 : line.indexOf('\t', p1 + 1);
    int p3 = p2 < 0 ? -1 : line.indexOf('\t', p2 + 1);
    String stored = p3 < 0 ? String() : line.substring(p3 + 1);

    if(stored.length() && memoryV2Matches(stored, needle)){
      removed++;
      continue;
    }

    output.println(line);
  }

  input.close();
  output.close();

  if(!removed){
    SD.remove(TEMP_PATH);
    return 0;
  }

  const char* BACKUP_PATH = "/elli_brain/memory_db.bak";
  SD.remove(BACKUP_PATH);

  if(!SD.rename(MEMORY_DB_PATH, BACKUP_PATH)){
    SD.remove(TEMP_PATH);
    return 0;
  }

  if(!SD.rename(TEMP_PATH, MEMORY_DB_PATH)){
    Serial.println("[MEMORY V2] Warning: SD memory database swap failed; restoring backup.");
    SD.rename(BACKUP_PATH, MEMORY_DB_PATH);
    SD.remove(TEMP_PATH);
    return 0;
  }

  SD.remove(BACKUP_PATH);
  return removed;
}

static void showSessionMemoryInternal(){
  Serial.println();
  Serial.println("========== SESSION MEMORY ==========");
  if(!sessionMemoryUsed){
    Serial.println("(empty)");
  }else{
    for(uint8_t i = 0; i < sessionMemoryUsed; i++){
      Serial.print(i + 1);
      Serial.print(". ");
      Serial.println(sessionMemories[i]);
    }
  }
  Serial.println("====================================");
}

} // namespace

void elliMemoryV2Begin(){
  clearSessionMemory();
  Serial.println("[MEMORY V2] Session memory + explicit forget commands ready.");
}

void clearSessionMemory(){
  for(uint8_t i = 0; i < MAX_SESSION_MEMORIES; i++) sessionMemories[i] = "";
  sessionMemoryUsed = 0;
}

int sessionMemoryCount(){
  return sessionMemoryUsed;
}

String memoryV2Status(){
  String out = "Memory V2: ";
  out += String(memoryCount());
  out += " persistent NVS memories and ";
  out += String(sessionMemoryCount());
  out += " temporary session memories.";
  return out;
}

bool handleMemoryV2Intent(String q){
  q = normalizeInput(q);

  if(q == "memory v2 status" || q == "session memory status"){
    elliSay(memoryV2Status());
    return true;
  }

  if(q == "show session memory" ||
     q == "show temporary memory" ||
     q == "what do you remember this conversation" ||
     q == "what do you remember from this conversation"){
    showSessionMemoryInternal();
    elliSay(sessionMemoryUsed ? "That's everything I'm keeping only for this conversation." : "My temporary session memory is empty.");
    return true;
  }

  if(q.startsWith("session memory search ")){
    String needle = q.substring(22);
    needle.trim();
    if(!needle.length()){
      elliSay("Put a topic after 'session memory search'.");
      return true;
    }

    int found = 0;
    Serial.println();
    Serial.println("====== SESSION MEMORY SEARCH ======");
    for(uint8_t i = 0; i < sessionMemoryUsed; i++){
      if(memoryV2Matches(sessionMemories[i], needle)){
        found++;
        Serial.print("- ");
        Serial.println(sessionMemories[i]);
      }
    }
    Serial.println("===================================");
    elliSay(found ? "I found matching temporary memory in this conversation." : "I don't have a matching temporary memory in this conversation.");
    return true;
  }

  if(q == "clear session memory" ||
     q == "forget this conversation" ||
     q == "forget everything from this conversation"){
    clearSessionMemory();
    elliSay("Temporary conversation memory cleared. Your persistent profile memories are unchanged.");
    return true;
  }

  String fact;
  const char* const sessionPrefixes[] = {
    "remember for this conversation that ",
    "remember for this conversation ",
    "remember temporarily that ",
    "remember temporarily ",
    "remember this session that ",
    "session remember "
  };

  for(size_t i = 0; i < sizeof(sessionPrefixes)/sizeof(sessionPrefixes[0]); i++){
    String p = sessionPrefixes[i];
    if(q.startsWith(p)){
      fact = q.substring(p.length());
      fact.trim();
      if(!fact.length()){
        elliSay("Tell me what you want kept for this conversation.");
        return true;
      }
      return addSessionMemoryInternal(fact);
    }
  }

  if(q == "forget my name"){
    if(strlen(brainStore.ownerName)){
      brainStore.ownerName[0] = '\0';
      saveBrainStore();
      elliSay("Okay — I removed the saved name from my local profile.");
    }else{
      elliSay("I don't currently have a saved owner name.");
    }
    return true;
  }

  String needle;
  if(q.startsWith("forget that ")) needle = q.substring(12);
  else if(q.startsWith("forget memory about ")) needle = q.substring(20);
  else if(q.startsWith("forget memory ")) needle = q.substring(14);

  needle.trim();
  if(needle.length()){
    int sessionRemoved = forgetSessionMatches(needle);
    int nvsRemoved = forgetNvsMatches(needle);
    int sdRemoved = forgetSdMatches(needle);

    int total = sessionRemoved + nvsRemoved;
    // SD is a mirror/archive of persistent facts; don't double-count the same
    // logical fact in the user-facing total.
    if(!total && sdRemoved) total = sdRemoved;

    if(total){
      elliSay("Okay — I removed the matching memory from my local memory stores.");
    }else{
      elliSay("I couldn't find a saved memory matching that.");
    }
    return true;
  }

  return false;
}

// =====================================================
//                  PERSONAL MEMORY V3
// =====================================================
// Existing BrainMemoryRecord layout is intentionally preserved.
// category bit 7 = today-only memory; lower 7 bits = semantic category.
// reserved low byte = importance (0 legacy, 1 low, 2 normal, 3 important).
// =====================================================

namespace {

static const uint8_t MEMORY_V3_TODAY_FLAG = 0x80;
static const uint8_t MEMORY_IMPORTANCE_LOW = 1;
static const uint8_t MEMORY_IMPORTANCE_NORMAL = 2;
static const uint8_t MEMORY_IMPORTANCE_HIGH = 3;

static uint8_t memoryV3BaseCategory(uint8_t encoded){
  uint8_t base = encoded & 0x7F;
  if(base >= ELLI_MEM_GENERAL && base <= ELLI_MEM_STUDY) return base;
  // Legacy categories 1/2 remain valid and are shown as GENERAL/LEGACY.
  return base;
}

static bool memoryV3TodayOnly(uint8_t encoded){
  return (encoded & MEMORY_V3_TODAY_FLAG) != 0;
}

static uint8_t memoryV3Importance(const BrainMemoryRecord& r){
  uint8_t value = (uint8_t)(r.reserved & 0x00FF);
  return value <= MEMORY_IMPORTANCE_HIGH ? value : 0;
}

static String memoryV3ImportanceName(uint8_t importance){
  if(importance >= MEMORY_IMPORTANCE_HIGH) return "important";
  if(importance == MEMORY_IMPORTANCE_NORMAL) return "normal";
  if(importance == MEMORY_IMPORTANCE_LOW) return "low";
  return "legacy";
}

static int memoryV3ChooseSlot(){
  for(uint8_t i = 0; i < MAX_BRAIN_MEMORIES; i++){
    if(!brainStore.memories[i].active) return i;
  }

  // Protect important memories when the 18-slot fast cache fills up.
  int best = 0;
  for(uint8_t i = 1; i < MAX_BRAIN_MEMORIES; i++){
    uint8_t impA = memoryV3Importance(brainStore.memories[i]);
    uint8_t impB = memoryV3Importance(brainStore.memories[best]);

    if(impA < impB){
      best = i;
      continue;
    }
    if(impA == impB && brainStore.memories[i].createdDay < brainStore.memories[best].createdDay){
      best = i;
    }
  }
  return best;
}

static bool memoryV3Store(
  String fact,
  uint8_t category,
  uint8_t importance,
  bool todayOnly
){
  fact = memoryV2CleanFact(fact);
  if(!fact.length()) return false;

  if(looksSensitiveForAutomaticMemory(fact)){
    elliSay("I won't save passwords, private addresses, financial secrets, or sensitive health details as personal memory.");
    return true;
  }

  category = memoryV3BaseCategory(category);
  if(category < ELLI_MEM_GENERAL || category > ELLI_MEM_STUDY) category = ELLI_MEM_GENERAL;
  importance = constrain(importance, MEMORY_IMPORTANCE_LOW, MEMORY_IMPORTANCE_HIGH);

  String normalized = normalizeInput(fact);

  // Update exact existing NVS entry instead of creating duplicates.
  for(uint8_t i = 0; i < MAX_BRAIN_MEMORIES; i++){
    if(!brainStore.memories[i].active) continue;
    if(normalizeInput(String(brainStore.memories[i].text)) != normalized) continue;

    brainStore.memories[i].category = category | (todayOnly ? MEMORY_V3_TODAY_FLAG : 0);
    brainStore.memories[i].reserved = importance;
    brainStore.memories[i].createdDay = currentBrainDay();
    saveBrainStore();

    if(!todayOnly) appendLongTermMemory(fact, category);

    elliSay(todayOnly
      ? "Updated. I'll keep that until the day changes."
      : "Updated. That memory is categorized and kept permanently.");
    return true;
  }

  int slot = memoryV3ChooseSlot();
  BrainMemoryRecord& r = brainStore.memories[slot];
  r.active = 1;
  r.category = category | (todayOnly ? MEMORY_V3_TODAY_FLAG : 0);
  r.reserved = importance;
  r.createdDay = currentBrainDay();
  copyBrainText(r.text, sizeof(r.text), fact);
  saveBrainStore();

  // Today-only facts intentionally never enter the long-term SD archive.
  if(!todayOnly) appendLongTermMemory(fact, category);

  String reply = "Saved as ";
  reply += elliMemoryCategoryName(category);
  reply += " memory";
  if(importance >= MEMORY_IMPORTANCE_HIGH) reply += " with high importance";
  reply += todayOnly ? " for today only." : " permanently.";
  elliSay(reply);
  return true;
}

static int memoryV3ExpireTodayOnly(){
  uint32_t day = currentBrainDay();
  if(day == 0) return 0; // Clock unavailable: do not delete anything blindly.

  int removed = 0;
  for(uint8_t i = 0; i < MAX_BRAIN_MEMORIES; i++){
    BrainMemoryRecord& r = brainStore.memories[i];
    if(!r.active || !memoryV3TodayOnly(r.category)) continue;
    if(r.createdDay == day) continue;

    r.active = 0;
    r.category = 0;
    r.reserved = 0;
    r.createdDay = 0;
    r.text[0] = '\0';
    removed++;
  }
  if(removed) saveBrainStore();
  return removed;
}

static int memoryV3ClearToday(){
  int removed = 0;
  for(uint8_t i = 0; i < MAX_BRAIN_MEMORIES; i++){
    BrainMemoryRecord& r = brainStore.memories[i];
    if(!r.active || !memoryV3TodayOnly(r.category)) continue;
    r.active = 0;
    r.category = 0;
    r.reserved = 0;
    r.createdDay = 0;
    r.text[0] = '\0';
    removed++;
  }
  if(removed) saveBrainStore();
  return removed;
}

static void memoryV3PrintCategory(uint8_t wanted){
  wanted = memoryV3BaseCategory(wanted);
  Serial.println();
  Serial.print("========== ");
  Serial.print(elliMemoryCategoryName(wanted));
  Serial.println(" MEMORIES ==========");
  int found = 0;
  for(uint8_t i = 0; i < MAX_BRAIN_MEMORIES; i++){
    const BrainMemoryRecord& r = brainStore.memories[i];
    if(!r.active || memoryV3BaseCategory(r.category) != wanted) continue;
    found++;
    Serial.print(found); Serial.print(". "); Serial.print(r.text);
    Serial.print(" [");
    Serial.print(memoryV3ImportanceName(memoryV3Importance(r)));
    if(memoryV3TodayOnly(r.category)) Serial.print(", today-only");
    Serial.println("]");
  }
  if(!found) Serial.println("(none in the fast NVS cache)");
  Serial.println("====================================");
}

static bool memoryV3CategoryFromText(String text, uint8_t& category){
  text = normalizeInput(text);
  if(text == "person" || text == "people" || text == "family") category = ELLI_MEM_PERSON;
  else if(text == "preference" || text == "preferences" || text == "likes") category = ELLI_MEM_PREFERENCE;
  else if(text == "project" || text == "projects") category = ELLI_MEM_PROJECT;
  else if(text == "fact" || text == "facts") category = ELLI_MEM_FACT;
  else if(text == "note" || text == "notes") category = ELLI_MEM_NOTE;
  else if(text == "device" || text == "devices" || text == "hardware") category = ELLI_MEM_DEVICE;
  else if(text == "study" || text == "school" || text == "education") category = ELLI_MEM_STUDY;
  else if(text == "general") category = ELLI_MEM_GENERAL;
  else return false;
  return true;
}

static String memoryV3StripThat(String s){
  s.trim();
  if(s.startsWith("that ")) s = s.substring(5);
  s.trim();
  return s;
}

} // namespace

const char* elliMemoryCategoryName(uint8_t encodedCategory){
  switch(memoryV3BaseCategory(encodedCategory)){
    case ELLI_MEM_PERSON: return "PERSON";
    case ELLI_MEM_PREFERENCE: return "PREFERENCE";
    case ELLI_MEM_PROJECT: return "PROJECT";
    case ELLI_MEM_FACT: return "FACT";
    case ELLI_MEM_NOTE: return "NOTE";
    case ELLI_MEM_DEVICE: return "DEVICE";
    case ELLI_MEM_STUDY: return "STUDY";
    case ELLI_MEM_GENERAL: return "GENERAL";
    default: return "LEGACY";
  }
}

uint8_t elliInferMemoryCategory(const String& factRaw){
  String f = normalizeInput(factRaw);

  if(f.indexOf("favorite") >= 0 || f.indexOf("favourite") >= 0 ||
     f.startsWith("i like ") || f.startsWith("i love ") ||
     f.indexOf(" i prefer ") >= 0 || f.startsWith("i prefer ")) return ELLI_MEM_PREFERENCE;

  if(f.indexOf("my project") >= 0 || f.indexOf("project ") >= 0 ||
     f.indexOf("working on ") >= 0 || f.indexOf("building ") >= 0) return ELLI_MEM_PROJECT;

  if(f.indexOf("my mother") >= 0 || f.indexOf("my father") >= 0 ||
     f.indexOf("my sister") >= 0 || f.indexOf("my brother") >= 0 ||
     f.indexOf("my friend") >= 0 || f.indexOf("my name") >= 0 ||
     f.indexOf("my family") >= 0) return ELLI_MEM_PERSON;

  if(f.indexOf("esp32") >= 0 || f.indexOf("sensor") >= 0 ||
     f.indexOf("display") >= 0 || f.indexOf("relay") >= 0 ||
     f.indexOf("speaker") >= 0 || f.indexOf("microphone") >= 0) return ELLI_MEM_DEVICE;

  if(f.indexOf("study") >= 0 || f.indexOf("school") >= 0 ||
     f.indexOf("exam") >= 0 || f.indexOf("class ") >= 0 ||
     f.indexOf("subject") >= 0) return ELLI_MEM_STUDY;

  return ELLI_MEM_FACT;
}

void elliMemoryV3Begin(){
  int expired = memoryV3ExpireTodayOnly();
  Serial.print("[MEMORY V3] Categories + importance + today-only memory ready");
  if(expired){
    Serial.print("; expired ");
    Serial.print(expired);
    Serial.print(" old today-only memories");
  }
  Serial.println(".");
}

String memoryV3Status(){
  int today = 0;
  int important = 0;
  for(uint8_t i = 0; i < MAX_BRAIN_MEMORIES; i++){
    if(!brainStore.memories[i].active) continue;
    if(memoryV3TodayOnly(brainStore.memories[i].category)) today++;
    if(memoryV3Importance(brainStore.memories[i]) >= MEMORY_IMPORTANCE_HIGH) important++;
  }

  String out = "Memory V3: ";
  out += String(memoryCount());
  out += " persistent-cache records, ";
  out += String(sessionMemoryCount());
  out += " session records, ";
  out += String(today);
  out += " today-only, ";
  out += String(important);
  out += " important.";
  return out;
}

bool handleMemoryV3Intent(String q){
  q = normalizeInput(q);

  if(q == "memory v3 status" || q == "advanced memory status"){
    elliSay(memoryV3Status());
    return true;
  }

  if(q == "memory categories" || q == "show memory categories"){
    elliSay("Memory categories are person, preference, project, fact, note, device, study, and general. Memories can also be important, today-only, session-only, or permanent.");
    return true;
  }

  if(q == "forget today's memories" || q == "forget todays memories" || q == "clear today memory"){
    int removed = memoryV3ClearToday();
    elliSay(removed ? "Today's temporary persistent memories are cleared." : "I don't have any today-only memories to clear.");
    return true;
  }

  if(q.startsWith("show ") && q.endsWith(" memories")){
    String cat = q.substring(5, q.length() - 9);
    cat.trim();
    uint8_t category;
    if(memoryV3CategoryFromText(cat, category)){
      memoryV3PrintCategory(category);
      elliSay("I printed the matching categorized memories in Serial Monitor.");
      return true;
    }
  }

  // Explicit category grammar: "remember as project that ..."
  if(q.startsWith("remember as ")){
    String rest = q.substring(12);
    int split = rest.indexOf(' ');
    if(split > 0){
      String catText = rest.substring(0, split);
      String fact = memoryV3StripThat(rest.substring(split + 1));
      uint8_t category;
      if(memoryV3CategoryFromText(catText, category) && fact.length()){
        return memoryV3Store(fact, category, MEMORY_IMPORTANCE_NORMAL, false);
      }
    }
  }

  // "remember project that ..." / "remember preference ..."
  const char* categoryWords[] = {"person", "preference", "project", "fact", "note", "device", "study", "general"};
  for(size_t i = 0; i < sizeof(categoryWords)/sizeof(categoryWords[0]); i++){
    String prefix = "remember " + String(categoryWords[i]) + " ";
    if(q.startsWith(prefix)){
      uint8_t category;
      memoryV3CategoryFromText(categoryWords[i], category);
      String fact = memoryV3StripThat(q.substring(prefix.length()));
      if(!fact.length()){
        elliSay("Tell me what you want stored in that memory category.");
        return true;
      }
      return memoryV3Store(fact, category, MEMORY_IMPORTANCE_NORMAL, false);
    }
  }

  if(q.startsWith("remember important ")){
    String fact = memoryV3StripThat(q.substring(19));
    if(!fact.length()){
      elliSay("Tell me what important fact you want remembered.");
      return true;
    }
    return memoryV3Store(fact, elliInferMemoryCategory(fact), MEMORY_IMPORTANCE_HIGH, false);
  }

  if(q.startsWith("remember permanently ")){
    String fact = memoryV3StripThat(q.substring(21));
    if(!fact.length()){
      elliSay("Tell me what you want remembered permanently.");
      return true;
    }
    return memoryV3Store(fact, elliInferMemoryCategory(fact), MEMORY_IMPORTANCE_NORMAL, false);
  }

  const char* todayPrefixes[] = {
    "remember for today ",
    "remember today ",
    "remember this just for today ",
    "remember only for today "
  };
  for(size_t i = 0; i < sizeof(todayPrefixes)/sizeof(todayPrefixes[0]); i++){
    String prefix = todayPrefixes[i];
    if(q.startsWith(prefix)){
      String fact = memoryV3StripThat(q.substring(prefix.length()));
      if(!fact.length()){
        elliSay("Tell me what you want kept only for today.");
        return true;
      }
      return memoryV3Store(fact, elliInferMemoryCategory(fact), MEMORY_IMPORTANCE_NORMAL, true);
    }
  }

  // Plain explicit remember now gains automatic semantic categorization.
  // Keep task grammar ("remember to ...") and session-only grammar owned by
  // their existing handlers.
  if(q.startsWith("remember that ")){
    String fact = q.substring(14);
    fact.trim();
    if(fact.length())
      return memoryV3Store(fact, elliInferMemoryCategory(fact), MEMORY_IMPORTANCE_NORMAL, false);
  }

  if(q.startsWith("remember ") &&
     !q.startsWith("remember to ") &&
     !q.startsWith("remember for this conversation ") &&
     !q.startsWith("remember temporarily ") &&
     !q.startsWith("remember this session ")){
    String fact = q.substring(9);
    fact.trim();
    if(fact.length())
      return memoryV3Store(fact, elliInferMemoryCategory(fact), MEMORY_IMPORTANCE_NORMAL, false);
  }

  return false;
}
