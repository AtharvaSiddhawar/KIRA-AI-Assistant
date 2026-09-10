#include <Arduino.h>
#include "elli_language.h"

// These still live in kira_brain.ino during migration.
String normalizeInput(String s);
int smallEditDistance(String a,String b,int cutoff);


// =====================================================
//              WORD-BOUNDARY UTILITIES
// =====================================================
//
// IMPORTANT:
//
// Old code used:
//
//     input.replace("cant ","cannot ");
//
// That accidentally changed:
//
//     significant
//
// because it contains:
//
//     ...cant
//
// giving:
//
//     significannot
//
// All language replacements are now boundary-aware.
// =====================================================

bool elliIsWordChar(char c){

  return
    isalnum(
      (unsigned char)c
    )

    ||

    c=='_';
}


bool elliBoundaryBefore(
  const String& text,
  int position
){

  if(position<=0){
    return true;
  }

  return
    !elliIsWordChar(
      text[position-1]
    );
}


bool elliBoundaryAfter(
  const String& text,
  int position
){

  if(position>=text.length()){
    return true;
  }

  return
    !elliIsWordChar(
      text[position]
    );
}


// =====================================================
//            SAFE WORD / PHRASE REPLACE
// =====================================================

void elliReplaceBounded(
  String& text,
  const String& from,
  const String& to
){

  if(!from.length()){
    return;
  }

  int searchFrom=0;

  while(searchFrom<text.length()){

    int pos=
      text.indexOf(
        from,
        searchFrom
      );

    if(pos<0){
      break;
    }


    int after=
      pos+
      from.length();


    bool beforeOK=
      elliBoundaryBefore(
        text,
        pos
      );


    bool afterOK=
      elliBoundaryAfter(
        text,
        after
      );


    if(
      beforeOK &&
      afterOK
    ){

      text=
        text.substring(
          0,
          pos
        )

        +

        to

        +

        text.substring(
          after
        );


      searchFrom=
        pos+
        to.length();
    }

    else{

      searchFrom=
        pos+1;
    }
  }
}


// =====================================================
//             SPEECH / TYPING NORMALIZER
// =====================================================
//
// Only HIGH-CONFIDENCE corrections belong here.
//
// Semantic interpretation belongs elsewhere.
//
// Examples:
//
//   tution  -> tuition
//   rember  -> remember
//   alram   -> alarm
//
// But:
//
//   time
//
// is NEVER changed to:
//
//   timer
//
// =====================================================

String normalizeSpeechUtterance(String input){

  input.trim();
  input.toLowerCase();


  // ===================================================
  // HIGH-CONFIDENCE WORD CORRECTIONS
  // ===================================================

  struct Replacement {

    const char* from;
    const char* to;
  };


  const Replacement replacements[]={

    // ===================================================
    // UNIVERSAL ENGLISH CONTRACTIONS / CASUAL FORMS
    // ===================================================
    //
    // Only unambiguous/high-confidence expansions live here.
    // Open vocabulary is NOT spell-corrected aggressively; that would
    // recreate bugs such as time -> timer.
    // ===================================================

    {"i'm","i am"},
    {"you're","you are"},
    {"we're","we are"},
    {"they're","they are"},

    {"i've","i have"},
    {"you've","you have"},
    {"we've","we have"},
    {"they've","they have"},

    {"i'll","i will"},
    {"you'll","you will"},
    {"we'll","we will"},
    {"they'll","they will"},
    {"he'll","he will"},
    {"she'll","she will"},
    {"it'll","it will"},

    {"can't","cannot"},
    {"cannot","cannot"},
    {"won't","will not"},
    {"don't","do not"},
    {"doesn't","does not"},
    {"didn't","did not"},
    {"isn't","is not"},
    {"aren't","are not"},
    {"wasn't","was not"},
    {"weren't","were not"},
    {"haven't","have not"},
    {"hasn't","has not"},
    {"hadn't","had not"},
    {"couldn't","could not"},
    {"shouldn't","should not"},
    {"wouldn't","would not"},
    {"mustn't","must not"},

    {"what's","what is"},
    {"where's","where is"},
    {"when's","when is"},
    {"who's","who is"},
    {"why's","why is"},
    {"how's","how is"},
    {"that's","that is"},
    {"there's","there is"},

    // High-confidence missing-space conversational forms.
    // These are common when typing quickly in Serial Monitor / STT cleanup.
    {"tellme","tell me"},

    // Common conversational shorthand that has one clear meaning.
    {"idk","i do not know"},
    {"imo","in my opinion"},
    {"imho","in my opinion"},
    {"btw","by the way"},
    {"rn","right now"},
    {"asap","as soon as possible"},

    // Common STT / typing slips.
    {"tution","tuition"},
    {"tuttion","tuition"},

    {"remeber","remember"},
    {"rember","remember"},

    {"remindr","reminder"},

    {"tomorow","tomorrow"},
    {"tommorow","tomorrow"},

    {"alaram","alarm"},
    {"alram","alarm"},

    {"stowatch","stopwatch"},
    {"stopwach","stopwatch"},

    {"calender","calendar"},
    {"calandar","calendar"},

    // Broad high-confidence English slips.
    {"becuase","because"},
    {"beacuse","because"},
    {"definately","definitely"},
    {"definetly","definitely"},
    {"seperate","separate"},
    {"recieve","receive"},
    {"beleive","believe"},
    {"wierd","weird"},
    {"alot","a lot"},
    {"quesion","question"},
    {"qustion","question"},
    {"answere","answer"},
    {"wether","whether"},
    {"wheather","weather"},
    {"temprature","temperature"},
    {"tempreature","temperature"},
    {"popluation","population"},
    {"univercity","university"},
    {"goverment","government"},
    {"scince","science"},
    {"tecnology","technology"},

    // Family / relationship vocabulary.
    {"siter","sister"},
    {"sistr","sister"},
    {"sistes","sisters"},
    {"brothr","brother"},
    {"siblng","sibling"},
    {"freind","friend"},
    {"frnd","friend"},
    {"famliy","family"},
    {"relaton","relation"},
    {"relashionship","relationship"},

    // Casual but semantically safe forms.
    {"becoz","because"},
    {"cuz","because"},
    {"coz","because"},

    {"wht","what"},
    {"wat","what"},

    {"abt","about"},
    {"pls","please"},
    {"plz","please"},

    {"lemme","let me"},
    {"gimme","give me"},

    {"gonna","going to"},
    {"wanna","want to"},
    {"gotta","have to"},
    {"kinda","kind of"},
    {"sorta","sort of"},

    // High-confidence contractions with missing apostrophes.
    {"im","i am"},
    {"ive","i have"},

    {"dont","do not"},
    {"cant","cannot"},
    {"isnt","is not"},
    {"arent","are not"},
    {"wasnt","was not"},
    {"werent","were not"},
    {"havent","have not"},
    {"hasnt","has not"},
    {"hadnt","had not"},

    {"whats","what is"},
    {"hows","how is"},
    {"wheres","where is"},
    {"whos","who is"},
    {"whens","when is"},
    {"whys","why is"},

    {"youre","you are"},
    {"youve","you have"},
    {"theyre","they are"},
    {"weve","we have"},
    {"thats","that is"},
    {"theres","there is"},

    {"didnt","did not"},
    {"doesnt","does not"},

    {"wont","will not"},
    {"couldnt","could not"},
    {"shouldnt","should not"},
    {"wouldnt","would not"}
  };


  for(
    size_t i=0;
    i<
    sizeof(replacements)/
    sizeof(replacements[0]);
    i++
  ){

    elliReplaceBounded(

      input,

      replacements[i].from,

      replacements[i].to
    );
  }


  // ===================================================
  // MULTI-WORD HIGH-CONFIDENCE STT FORMS
  // ===================================================

  elliReplaceBounded(
    input,
    "wi fi",
    "wifi"
  );

  elliReplaceBounded(
    input,
    "micro controller",
    "microcontroller"
  );

  elliReplaceBounded(
    input,
    "esp 32",
    "esp32"
  );

  elliReplaceBounded(
    input,
    "e s p 32",
    "esp32"
  );


  // ===================================================
  // COMMON SPOKEN GRAMMAR FORMS
  // ===================================================

  elliReplaceBounded(input,"could you please","could you");
  elliReplaceBounded(input,"would you please","would you");
  elliReplaceBounded(input,"can you please","can you");
  elliReplaceBounded(input,"tell me please","tell me");


  // ===================================================
  // POLITE WRAPPER CANONICALIZATION
  // ===================================================
  //
  // Examiners may phrase the same request as:
  //   "remind me to..."
  //   "can you remind me to..."
  //   "could you please remind me to..."
  //
  // Strip the modal wrapper ONLY when the remainder begins with a
  // high-confidence request verb. This keeps open questions such as
  // "can you use X?" intact.
  // ===================================================

  const char* const politePrefixes[]={
    "can you ",
    "could you ",
    "would you ",
    "will you ",
    "please "
  };

  const char* const requestVerbs[]={
    "remind ","remember ","add ","create ",
    "set ","start ","stop ","pause ","resume ",
    "cancel ","delete ","remove ","clear ","reset ",
    "turn ","switch ","power ","open ","close ",
    "show ","list ","tell ","give ",
    "explain ","define ","describe ","compare ",
    "calculate ","compute ","identify ","find ",
    "search ","look up ","check "
  };

  bool stripped=true;

  while(stripped){
    stripped=false;

    for(size_t p=0;p<sizeof(politePrefixes)/sizeof(politePrefixes[0]);p++){
      String prefix=politePrefixes[p];

      if(!input.startsWith(prefix)) continue;

      String rest=input.substring(prefix.length());
      rest.trim();

      bool safeRequest=false;

      for(size_t v=0;v<sizeof(requestVerbs)/sizeof(requestVerbs[0]);v++){
        if(rest.startsWith(requestVerbs[v])){
          safeRequest=true;
          break;
        }
      }

      if(safeRequest){
        input=rest;
        stripped=true;
        break;
      }
    }
  }


  // ===================================================
  // DOTTED CLOCK NOTATION (DECIMAL-SAFE)
  // ===================================================
  //
  // Convert unmistakable clock forms such as:
  //   "at 3.45 pm" -> "at 3:45 pm"
  //   "alarm at 18.30" -> "alarm at 18:30"
  //
  // DO NOT convert ordinary decimals:
  //   2.5 kg
  //   2.50 volts
  //   18.75 percent
  //
  // The old rule converted every digit-dot-digit sequence and therefore
  // changed 2.5 -> 2:5; normalizeInput then removed ':' and produced 25.
  // ===================================================

  for(int i=1;i<input.length()-1;i++){
    if(input[i]!='.' || !isdigit((unsigned char)input[i-1]) || !isdigit((unsigned char)input[i+1])) continue;

    int left=i-1;
    while(left>0 && isdigit((unsigned char)input[left-1])) left--;

    int right=i+1;
    while(right+1<input.length() && isdigit((unsigned char)input[right+1])) right++;

    int hourDigits=i-left;
    int minuteDigits=right-i;
    if(hourDigits<1 || hourDigits>2 || minuteDigits!=2) continue;

    int hour=input.substring(left,i).toInt();
    int minute=input.substring(i+1,right+1).toInt();
    if(hour<0 || hour>23 || minute<0 || minute>59) continue;

    int after=right+1;
    while(after<input.length() && input[after]==' ') after++;

    bool hasMeridiem=false;
    if(after+1<input.length()){
      String suffix=input.substring(after);
      hasMeridiem=suffix.startsWith("am") || suffix.startsWith("pm");
    }

    String before=input.substring(0,left);
    before.trim();
    int lastSpace=before.lastIndexOf(' ');
    String previousWord=lastSpace>=0 ? before.substring(lastSpace+1) : before;

    bool explicitTimeContext=previousWord=="at" || previousWord=="around" || previousWord=="time";
    if(hasMeridiem || explicitTimeContext) input.setCharAt(i,':');
  }


  // ===================================================
  // WHITESPACE
  // ===================================================

  while(
    input.indexOf(
      "  "
    )>=0
  ){

    input.replace(
      "  ",
      " "
    );
  }


  input.trim();

  return input;
}


// =====================================================
//               SAFE FUZZY INTENT WORDS
// =====================================================
//
// Different-length automatic corrections are BLOCKED.
//
// This prevents:
//
//     time -> timer
//
// Known missing-letter errors belong in the explicit
// high-confidence dictionary above.
// =====================================================

String fuzzyNormalizeIntentWords(String q){

  q=
    normalizeInput(q);


  const char* canonical[]={

    "remember",
    "remind",
    "reminder",
    "forget",

    "task",
    "tasks",

    "complete",
    "completed",

    "daily",
    "everyday",

    "tuition",

    "appointment",
    "meeting",

    "tomorrow",
    "today",

    "alarm",
    "timer",
    "stopwatch",

    "start",
    "stop",

    "turn",
    "switch",

    "light",
    "fan",
    "charger",

    "weather",

    "calculate",
    "convert",

    "name",

    "sister",
    "sisters",
    "brother",
    "brothers",
    "sibling",
    "siblings",

    "mother",
    "father",
    "parent",
    "parents",

    "friend",
    "friends",
    "cousin",
    "cousins",

    "favorite",
    "favourite",

    "motivate",
    "motivation",

    "inspire",
    "inspiration",

    "sad",
    "stressed",
    "anxious",
    "worried",
    "angry",
    "lonely",

    "focus",

    "routine",
    "routines",

    "schedule",
    "scheduled",

    "deadline",
    "due",

    "memory",
    "memories",

    "profile",

    "morning",
    "afternoon",
    "evening",
    "night"
  };


  String output="";

  int pos=0;


  while(pos<q.length()){

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
      end=q.length();
    }


    String word=
      q.substring(
        pos,
        end
      );


    String replacement=
      word;


    if(word.length()>=4){

      int bestDistance=3;

      String bestWord=
        word;


      for(
        size_t i=0;
        i<
        sizeof(canonical)/
        sizeof(canonical[0]);
        i++
      ){

        String candidate=
          canonical[i];


        // ===============================================
        // SAFETY RULE:
        // only same-length fuzzy substitutions.
        // ===============================================

        if(
          word.length()
          !=
          candidate.length()
        ){

          continue;
        }


        int distance=
          smallEditDistance(
            word,
            candidate,
            1
          );


        if(
          distance<
          bestDistance
        ){

          bestDistance=
            distance;

          bestWord=
            candidate;


          if(distance==0){
            break;
          }
        }
      }


      if(bestDistance<=1){

        replacement=
          bestWord;
      }
    }


    if(output.length()){
      output+=" ";
    }


    output+=
      replacement;


    pos=
      end+1;
  }


  output.trim();

  return output;
}