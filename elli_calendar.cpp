#include <Arduino.h>
#include <WiFi.h>

#include "elli_calendar.h"


// =====================================================
//        FUNCTIONS PROVIDED BY kira_brain.ino
// =====================================================

String normalizeInput(String s);

void elliSay(const String& s);

bool httpsGet(
  const String& url,
  String& body,
  int& code
);

String urlEncode(
  const String& s
);

String extractJsonString(
  const String& json,
  const String& key
);


// =====================================================
//                  CALENDAR MODEL
// =====================================================

struct ElliCalendarDate {

  bool valid;

  int day;
  int month;
  int year;

  bool explicitYear;
};


// =====================================================
//                 BASIC TEXT HELPERS
// =====================================================

String calendarCollapseSpaces(String s){

  while(
    s.indexOf("  ")>=0
  ){

    s.replace(
      "  ",
      " "
    );
  }

  s.trim();

  return s;
}


String calendarCompact(String s){

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


// =====================================================
//               BASIC HTML -> TEXT
// =====================================================

String calendarHtmlToText(String html){

  html.replace(
    "</li>",
    "\n"
  );

  html.replace(
    "</p>",
    "\n"
  );

  html.replace(
    "<br>",
    "\n"
  );

  html.replace(
    "<br/>",
    "\n"
  );

  html.replace(
    "<br />",
    "\n"
  );


  String out="";

  bool insideTag=false;


  for(size_t i=0;i<html.length();i++){

    char c=html[i];


    if(c=='<'){

      insideTag=true;

      continue;
    }


    if(c=='>'){

      insideTag=false;

      continue;
    }


    if(!insideTag){

      out+=c;
    }
  }


  out.replace(
    "&nbsp;",
    " "
  );

  out.replace(
    "&#160;",
    " "
  );

  out.replace(
    "&amp;",
    "&"
  );

  out.replace(
    "&quot;",
    "\""
  );

  out.replace(
    "&#39;",
    "'"
  );

  out.replace(
    "&apos;",
    "'"
  );

  out.replace(
    "&ndash;",
    "-"
  );

  out.replace(
    "&mdash;",
    "-"
  );


  while(
    out.indexOf("\r")>=0
  ){

    out.replace(
      "\r",
      ""
    );
  }


  while(
    out.indexOf("\n\n")>=0
  ){

    out.replace(
      "\n\n",
      "\n"
    );
  }


  out=
    calendarCollapseSpaces(
      out
    );


  return out;
}


// =====================================================
//                   MONTH HANDLING
// =====================================================

String calendarMonthName(int month){

  const char* months[]={

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
      months[month-1]
    );
}


int calendarMonthNumber(String token){

  token.toLowerCase();

  String clean="";


  for(size_t i=0;i<token.length();i++){

    char c=token[i];

    if(
      isalpha(
        (unsigned char)c
      )
    ){

      clean+=c;
    }
  }


  if(clean.length()<3){

    return 0;
  }


  String first=
    clean.substring(
      0,
      3
    );


  const char* months[]={

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

    if(first==months[i]){

      return i+1;
    }
  }


  return 0;
}


// =====================================================
//                  NUMBER / ORDINAL
// =====================================================

int calendarNumber(String token){

  token.toLowerCase();

  token.trim();


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


  String digits="";


  for(size_t i=0;i<token.length();i++){

    char c=token[i];

    if(
      isdigit(
        (unsigned char)c
      )
    ){

      digits+=c;
    }

    else if(digits.length()){

      return -1;
    }
  }


  if(!digits.length()){

    return -1;
  }


  return
    digits.toInt();
}


// =====================================================
//                      TOKENIZER
// =====================================================

int calendarTokenize(
  String text,
  String output[],
  int maximum
){

  text.toLowerCase();


  text.replace(
    "/",
    " "
  );

  text.replace(
    ",",
    " "
  );


  int count=0;

  int position=0;


  while(
    position<text.length() &&
    count<maximum
  ){

    while(
      position<text.length() &&
      text[position]==' '
    ){

      position++;
    }


    if(position>=text.length()){

      break;
    }


    int end=
      text.indexOf(
        ' ',
        position
      );


    if(end<0){

      end=
        text.length();
    }


    String token=
      text.substring(
        position,
        end
      );


    token.trim();


    if(token.length()){

      output[count++]=
        token;
    }


    position=
      end+1;
  }


  return count;
}


// =====================================================
//                CURRENT LOCAL YEAR
// =====================================================
//
// NEVER hard-coded.
//
// No year in user request:
//      use KIRA's current NTP year.
//
// =====================================================

int calendarCurrentYear(){

  struct tm now;


  if(
    !getLocalTime(
      &now,
      500
    )
  ){

    return 0;
  }


  return
    now.tm_year+
    1900;
}


int calendarRequestedYear(String q){

  String tokens[30];


  int count=
    calendarTokenize(
      q,
      tokens,
      30
    );


  for(int i=0;i<count;i++){

    int number=
      calendarNumber(
        tokens[i]
      );


    if(
      number>=1900 &&
      number<=2200
    ){

      return number;
    }
  }


  return
    calendarCurrentYear();
}


// =====================================================
//                   DATE VALIDATION
// =====================================================

bool calendarValidDate(
  int day,
  int month,
  int year
){

  if(
    day<1 ||
    day>31 ||
    month<1 ||
    month>12 ||
    year<1900 ||
    year>2200
  ){

    return false;
  }


  struct tm value={};


  value.tm_year=
    year-1900;

  value.tm_mon=
    month-1;

  value.tm_mday=
    day;

  value.tm_hour=12;

  value.tm_isdst=-1;


  time_t epoch=
    mktime(
      &value
    );


  if(epoch==(time_t)-1){

    return false;
  }


  struct tm check;


  localtime_r(
    &epoch,
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
//                   RELATIVE DATE
// =====================================================

bool calendarRelativeDate(
  String q,
  ElliCalendarDate& result
){

  q=
    normalizeInput(q);


  int difference=999;


  if(
    q.indexOf(
      "day after tomorrow"
    )>=0
  ){

    difference=2;
  }


  else if(
    q.indexOf(
      "tomorrow"
    )>=0
  ){

    difference=1;
  }


  else if(
    q.indexOf(
      "yesterday"
    )>=0
  ){

    difference=-1;
  }


  else if(
    q.indexOf(
      "today"
    )>=0
  ){

    difference=0;
  }


  if(difference==999){

    return false;
  }


  struct tm now;


  if(
    !getLocalTime(
      &now,
      500
    )
  ){

    return false;
  }


  now.tm_hour=12;
  now.tm_min=0;
  now.tm_sec=0;


  now.tm_mday+=
    difference;


  time_t epoch=
    mktime(
      &now
    );


  if(epoch==(time_t)-1){

    return false;
  }


  struct tm output;


  localtime_r(
    &epoch,
    &output
  );


  result.valid=true;

  result.day=
    output.tm_mday;

  result.month=
    output.tm_mon+1;

  result.year=
    output.tm_year+1900;

  result.explicitYear=false;


  return true;
}


// =====================================================
//                   DATE PARSER
// =====================================================

ElliCalendarDate calendarParseDate(
  String q
){

  ElliCalendarDate result={

    false,

    0,
    0,
    0,

    false
  };


  q=
    normalizeInput(q);


  if(
    calendarRelativeDate(
      q,
      result
    )
  ){

    return result;
  }


  String tokens[30];


  int count=
    calendarTokenize(
      q,
      tokens,
      30
    );


  if(count==0){

    return result;
  }


  // ===================================================
  // WRITTEN MONTH
  // ===================================================

  int monthIndex=-1;

  int month=0;


  for(int i=0;i<count;i++){

    int value=
      calendarMonthNumber(
        tokens[i]
      );


    if(value>0){

      month=value;

      monthIndex=i;

      break;
    }
  }


  int day=-1;


  if(monthIndex>=0){

    // -----------------------------------------------
    // Look close to month.
    // -----------------------------------------------

    for(
      int distance=1;
      distance<=3 &&
      day<0;
      distance++
    ){

      int before=
        monthIndex-
        distance;


      int after=
        monthIndex+
        distance;


      if(before>=0){

        int value=
          calendarNumber(
            tokens[before]
          );


        if(
          value>=1 &&
          value<=31
        ){

          day=value;
        }
      }


      if(
        day<0 &&
        after<count
      ){

        int value=
          calendarNumber(
            tokens[after]
          );


        if(
          value>=1 &&
          value<=31
        ){

          day=value;
        }
      }
    }
  }


  // ===================================================
  // NUMERIC DATE
  // ===================================================

  else{

    int values[8];

    int valueCount=0;


    for(
      int i=0;
      i<count &&
      valueCount<8;
      i++
    ){

      int number=
        calendarNumber(
          tokens[i]
        );


      if(number>=0){

        values[
          valueCount++
        ]=
          number;
      }
    }


    for(
      int i=0;
      i+1<valueCount;
      i++
    ){

      int first=
        values[i];


      int second=
        values[i+1];


      if(
        first>=1 &&
        first<=31 &&
        second>=1 &&
        second<=12
      ){

        day=first;
        month=second;

        break;
      }
    }
  }


  if(
    day<1 ||
    month<1
  ){

    return result;
  }


  int year=
    calendarRequestedYear(
      q
    );


  if(year==0){

    return result;
  }


  bool explicitYear=false;


  for(int i=0;i<count;i++){

    int number=
      calendarNumber(
        tokens[i]
      );


    if(
      number>=1900 &&
      number<=2200
    ){

      explicitYear=true;

      year=number;

      break;
    }
  }


  if(
    !calendarValidDate(
      day,
      month,
      year
    )
  ){

    return result;
  }


  result.valid=true;

  result.day=day;

  result.month=month;

  result.year=year;

  result.explicitYear=
    explicitYear;


  return result;
}


// =====================================================
//                   PRETTY DATE
// =====================================================

String calendarPrettyDate(
  const ElliCalendarDate& date
){

  if(!date.valid){

    return "";
  }


  return

    String(
      date.day
    )

    +

    " "

    +

    calendarMonthName(
      date.month
    )

    +

    " "

    +

    String(
      date.year
    );
}


// =====================================================
//                      WEEKDAY
// =====================================================

String calendarWeekday(
  const ElliCalendarDate& date
){

  if(!date.valid){

    return "";
  }


  struct tm value={};


  value.tm_year=
    date.year-1900;

  value.tm_mon=
    date.month-1;

  value.tm_mday=
    date.day;

  value.tm_hour=12;

  value.tm_isdst=-1;


  time_t epoch=
    mktime(
      &value
    );


  if(epoch==(time_t)-1){

    return "";
  }


  struct tm output;


  localtime_r(
    &epoch,
    &output
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
      days[
        output.tm_wday
      ]
    );
}


// =====================================================
//                   QUERY INTENTS
// =====================================================

bool calendarAsksWeekday(String q){

  q=
    normalizeInput(q);


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
    )>=0;
}


bool calendarAsksHistory(String q){

  q=
    normalizeInput(q);


  return

    q.indexOf(
      "what happened"
    )>=0

    ||

    q.indexOf(
      "historical"
    )>=0

    ||

    q.indexOf(
      "history"
    )>=0

    ||

    q.indexOf(
      "events on"
    )>=0;
}


bool calendarAsksObservance(String q){

  q=
    normalizeInput(q);


  // ===================================================
  // EXPLICIT FESTIVAL / OBSERVANCE LANGUAGE
  // ===================================================

  if(
    q.indexOf(
      "signific"
    )>=0

    ||

    q.indexOf(
      "special"
    )>=0

    ||

    q.indexOf(
      "celebrat"
    )>=0

    ||

    q.indexOf(
      "observ"
    )>=0

    ||

    q.indexOf(
      "festival"
    )>=0

    ||

    q.indexOf(
      "holiday"
    )>=0

    ||

    q.indexOf(
      "occasion"
    )>=0

    ||

    q.indexOf(
      "important day"
    )>=0
  ){

    return true;
  }


  // ===================================================
  // STRUCTURAL DATE -> EVENT QUESTIONS
  //
  // what is on 25 december
  // what is there on 25 december
  // which festival is on 25 december
  // ===================================================

  if(
    q.indexOf(
      "what is on "
    )>=0

    ||

    q.indexOf(
      "what is there on "
    )>=0

    ||

    q.indexOf(
      "what comes on "
    )>=0

    ||

    q.indexOf(
      "what occurs on "
    )>=0

    ||

    q.indexOf(
      "what occasion"
    )>=0

    ||

    q.indexOf(
      "what event"
    )>=0

    ||

    q.indexOf(
      "which event"
    )>=0

    ||

    q.indexOf(
      "which festival"
    )>=0

    ||

    q.indexOf(
      "which holiday"
    )>=0
  ){

    return true;
  }


  return false;
}


bool calendarAsksEventDate(String q){

  q=
    normalizeInput(q);


  return

    q.startsWith(
      "when is "
    )

    ||

    q.startsWith(
      "when does "
    )

    ||

    q.startsWith(
      "what date is "
    )

    ||

    q.startsWith(
      "what date does "
    );
}


// =====================================================
//                JSON STRING WALKER
// =====================================================

bool calendarJsonStringAfter(
  const String& json,
  const String& key,
  int startPosition,
  String& value,
  int& nextPosition
){

  String wanted=

    "\""+

    key+

    "\"";


  int keyPosition=
    json.indexOf(
      wanted,
      startPosition
    );


  if(keyPosition<0){

    return false;
  }


  int colon=
    json.indexOf(
      ':',
      keyPosition+
      wanted.length()
    );


  if(colon<0){

    return false;
  }


  int quote=
    json.indexOf(
      '"',
      colon+1
    );


  if(quote<0){

    return false;
  }


  String output="";

  bool escaped=false;


  for(
    int i=quote+1;
    i<json.length();
    i++
  ){

    char c=json[i];


    if(escaped){

      if(c=='n'){

        output+='\n';
      }

      else if(c=='r'){

        output+='\r';
      }

      else if(c=='t'){

        output+='\t';
      }

      else{

        output+=c;
      }


      escaped=false;

      continue;
    }


    if(c=='\\'){

      escaped=true;

      continue;
    }


    if(c=='"'){

      value=output;

      nextPosition=i+1;

      return true;
    }


    output+=c;
  }


  return false;
}


// =====================================================
//              WIKIPEDIA SECTION INDEX
// =====================================================

bool calendarFindWikiSection(
  const String& page,
  const String& wantedSection,
  String& sectionIndex
){

  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=parse"

    "&prop=sections"

    "&format=json"

    "&formatversion=2"

    "&page="+

    urlEncode(
      page
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

    return false;
  }


  int position=0;


  while(position<body.length()){

    String line;

    int afterLine=0;


    if(
      !calendarJsonStringAfter(
        body,
        "line",
        position,
        line,
        afterLine
      )
    ){

      break;
    }


    position=
      afterLine;


    if(
      normalizeInput(line)
      ==
      normalizeInput(wantedSection)
    ){

      String index;

      int afterIndex=0;


      if(
        calendarJsonStringAfter(
          body,
          "index",
          position,
          index,
          afterIndex
        )
      ){

        sectionIndex=index;

        return true;
      }
    }
  }


  return false;
}


// =====================================================
//          REGION SECTION ON EVENT PAGE
// =====================================================
//
// Generic solution for:
//
// Teachers' Day -> India
// Mother's Day -> country-specific section
// Father's Day -> region-specific section
//
// No event-specific date is hard-coded.
// =====================================================

bool calendarFindRegionSection(
  const String& page,
  const String& region,
  String& sectionIndex
){

  if(!region.length()){

    return false;
  }


  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=parse"

    "&prop=sections"

    "&format=json"

    "&formatversion=2"

    "&page="+

    urlEncode(
      page
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

    return false;
  }


  String compactRegion=
    calendarCompact(
      region
    );


  int position=0;


  while(position<body.length()){

    String line;

    int afterLine=0;


    if(
      !calendarJsonStringAfter(
        body,
        "line",
        position,
        line,
        afterLine
      )
    ){

      break;
    }


    position=
      afterLine;


    String compactLine=
      calendarCompact(
        line
      );


    bool match=
      compactLine==
      compactRegion;


    // India / Indian
    // America / American
    // etc.

    if(
      !match &&
      compactRegion.length()>=4 &&
      compactLine.length()>=4
    ){

      int shared=0;

      int limit=
        min(
          (int)compactRegion.length(),
          (int)compactLine.length()
        );


      while(
        shared<limit &&
        compactRegion[shared]
        ==
        compactLine[shared]
      ){

        shared++;
      }


      if(shared>=4){

        match=true;
      }
    }


    if(match){

      String index;

      int afterIndex=0;


      if(
        calendarJsonStringAfter(
          body,
          "index",
          position,
          index,
          afterIndex
        )
      ){

        sectionIndex=index;

        return true;
      }
    }
  }


  return false;
}


// =====================================================
//              FETCH WIKI SECTION
// =====================================================

bool calendarFetchWikiSectionByIndex(
  const String& page,
  const String& index,
  String& output
){

  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=parse"

    "&prop=text"

    "&format=json"

    "&formatversion=2"

    "&page="+

    urlEncode(
      page
    )

    +

    "&section="+

    urlEncode(
      index
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

    return false;
  }


  String html=
    extractJsonString(
      body,
      "text"
    );


  if(html.length()<10){

    return false;
  }


  output=
    calendarHtmlToText(
      html
    );


  if(output.length()>6000){

    output=
      output.substring(
        0,
        6000
      );
  }


  return
    output.length()>10;
}


bool calendarFetchWikiSection(
  const String& page,
  const String& section,
  String& output
){

  String index;


  if(
    !calendarFindWikiSection(
      page,
      section,
      index
    )
  ){

    return false;
  }


  return
    calendarFetchWikiSectionByIndex(
      page,
      index,
      output
    );
}


// =====================================================
//                   REGION PARSER
// =====================================================
//
// when is teachers day in india
//                         ^^^^^
//
// what is special on 15 august in india
//
// No country is hard-coded.
// =====================================================

String calendarExtractRegion(String q){

  q=
    normalizeInput(q);


  int position=
    q.lastIndexOf(
      " in "
    );


  int prefixLength=4;


  if(position<0){

    position=
      q.lastIndexOf(
        " for "
      );

    prefixLength=5;
  }


  if(position<0){

    return "";
  }


  String region=
    q.substring(
      position+
      prefixLength
    );


  String tokens[12];


  int count=
    calendarTokenize(
      region,
      tokens,
      12
    );


  String clean="";


  for(int i=0;i<count;i++){

    int number=
      calendarNumber(
        tokens[i]
      );


    // Remove year.

    if(
      number>=1900 &&
      number<=2200
    ){

      continue;
    }


    if(clean.length()){

      clean+=" ";
    }


    clean+=
      tokens[i];
  }


  clean.trim();


  // "in 2027" is not a region.

  if(!clean.length()){

    return "";
  }


  return clean;
}


// =====================================================
//               EVENT NAME PARSER
// =====================================================

String calendarExtractEventName(String q){

  q=
    normalizeInput(q);


  const char* prefixes[]={

    "when is ",
    "when does ",
    "when will ",

    "what date is ",
    "what date does ",

    "tell me when "
  };


  for(
    size_t i=0;
    i<
    sizeof(prefixes)/
    sizeof(prefixes[0]);
    i++
  ){

    String prefix=
      prefixes[i];


    if(q.startsWith(prefix)){

      q.remove(
        0,
        prefix.length()
      );

      break;
    }
  }


  // ===================================================
  // REMOVE REGION
  // ===================================================

  int regionPosition=
    q.lastIndexOf(
      " in "
    );


  if(regionPosition>=0){

    String possible=
      q.substring(
        regionPosition+4
      );


    bool yearOnly=true;


    String testTokens[8];


    int testCount=
      calendarTokenize(
        possible,
        testTokens,
        8
      );


    for(int i=0;i<testCount;i++){

      int number=
        calendarNumber(
          testTokens[i]
        );


      if(
        number<1900 ||
        number>2200
      ){

        yearOnly=false;

        break;
      }
    }


    if(!yearOnly){

      q=
        q.substring(
          0,
          regionPosition
        );
    }
  }


  // ===================================================
  // REMOVE YEAR
  // ===================================================

  String tokens[30];


  int count=
    calendarTokenize(
      q,
      tokens,
      30
    );


  String output="";


  for(int i=0;i<count;i++){

    int number=
      calendarNumber(
        tokens[i]
      );


    if(
      number>=1900 &&
      number<=2200
    ){

      continue;
    }


    if(output.length()){

      output+=" ";
    }


    output+=
      tokens[i];
  }


  output.trim();


  return output;
}


// =====================================================
//                SEARCH EVENT PAGE
// =====================================================

int calendarEventTitleScore(
  String wanted,
  String title
){

  String a=
    calendarCompact(
      wanted
    );


  String b=
    calendarCompact(
      title
    );


  if(!a.length()){

    return 0;
  }


  if(a==b){

    return 100;
  }


  if(
    b.indexOf(a)>=0
  ){

    return 85;
  }


  if(
    a.indexOf(b)>=0
  ){

    return 70;
  }


  String wantedTokens[12];


  int wantedCount=
    calendarTokenize(
      wanted,
      wantedTokens,
      12
    );


  int hits=0;


  String normalizedTitle=
    normalizeInput(
      title
    );


  for(int i=0;i<wantedCount;i++){

    if(
      normalizedTitle.indexOf(
        wantedTokens[i]
      )>=0
    ){

      hits++;
    }
  }


  if(wantedCount==0){

    return 0;
  }


  return
    (hits*60)/
    wantedCount;
}


bool calendarFindEventPage(
  String eventName,
  String& title
){

  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=query"

    "&list=search"

    "&srlimit=10"

    "&format=json"

    "&utf8=1"

    "&srsearch="+

    urlEncode(
      eventName
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

    return false;
  }


  int position=0;

  int bestScore=-1;

  String bestTitle="";


  for(int i=0;i<10;i++){

    String candidate;

    int after=0;


    if(
      !calendarJsonStringAfter(
        body,
        "title",
        position,
        candidate,
        after
      )
    ){

      break;
    }


    position=after;


    int score=
      calendarEventTitleScore(
        eventName,
        candidate
      );


    if(score>bestScore){

      bestScore=score;

      bestTitle=candidate;
    }
  }


  if(
    bestScore<35 ||
    !bestTitle.length()
  ){

    return false;
  }


  title=
    bestTitle;


  return true;
}


// =====================================================
//                 EVENT INTRO
// =====================================================

bool calendarFetchEventIntro(
  String title,
  String& intro
){

  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=query"

    "&prop=extracts"

    "&exintro=1"

    "&explaintext=1"

    "&exsentences=8"

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

    return false;
  }


  intro=
    extractJsonString(
      body,
      "extract"
    );


  intro.trim();


  return
    intro.length()>20;
}


// =====================================================
//             FIND DATE WITH CONTEXT
// =====================================================
//
// Rather than simply accepting the first month/day in an
// article, nearby words are scored.
//
// Positive:
//
// annually
// celebrated
// observed
// held
// takes place
//
// This reduces accidental extraction of unrelated dates.
// =====================================================

bool calendarFindBestDateInText(
  String text,
  int& foundDay,
  int& foundMonth
){

  String tokens[160];


  int count=
    calendarTokenize(
      text,
      tokens,
      160
    );


  int bestScore=-100;

  int bestDay=0;

  int bestMonth=0;


  String normalized=
    normalizeInput(
      text
    );


  for(int i=0;i<count;i++){

    int month=
      calendarMonthNumber(
        tokens[i]
      );


    if(month==0){

      continue;
    }


    int day=0;


    if(i>0){

      int value=
        calendarNumber(
          tokens[i-1]
        );


      if(
        value>=1 &&
        value<=31
      ){

        day=value;
      }
    }


    if(
      day==0 &&
      i+1<count
    ){

      int value=
        calendarNumber(
          tokens[i+1]
        );


      if(
        value>=1 &&
        value<=31
      ){

        day=value;
      }
    }


    if(day==0){

      continue;
    }


    String monthName=
      normalizeInput(
        calendarMonthName(
          month
        )
      );


    String dateA=

      String(day)+
      " "+
      monthName;


    String dateB=

      monthName+
      " "+
      String(day);


    int position=
      normalized.indexOf(
        dateA
      );


    if(position<0){

      position=
        normalized.indexOf(
          dateB
        );
    }


    int score=10;


    if(position>=0){

      int start=
        max(
          0,
          position-120
        );


      int end=
        min(
          (int)normalized.length(),
          position+180
        );


      String nearby=
        normalized.substring(
          start,
          end
        );


      const char* good[]={

        "annually",
        "annual",
        "celebrated",
        "celebrate",
        "observed",
        "observance",
        "held",
        "takes place",
        "falls on",
        "marked on",
        "commemorated",
        "festival",
        "holiday",
        "every year"
      };


      for(
        size_t g=0;
        g<
        sizeof(good)/
        sizeof(good[0]);
        g++
      ){

        if(
          nearby.indexOf(
            good[g]
          )>=0
        ){

          score+=20;
        }
      }


      if(
        nearby.indexOf(
          "established"
        )>=0

        ||

        nearby.indexOf(
          "founded"
        )>=0

        ||

        nearby.indexOf(
          "created"
        )>=0
      ){

        score-=15;
      }
    }


    if(score>bestScore){

      bestScore=score;

      bestDay=day;

      bestMonth=month;
    }
  }


  if(bestScore<0){

    return false;
  }


  foundDay=bestDay;

  foundMonth=bestMonth;


  return
    bestDay>0 &&
    bestMonth>0;
}


// =====================================================
//             REGIONAL AMBIGUITY
// =====================================================
//
// Teachers' Day is the perfect example:
//
// It is NOT "variable by year".
//
// It varies by COUNTRY.
//
// No region:
//
//     ask country.
//
// Region supplied:
//
//     resolve that country's section/search.
// =====================================================

bool calendarLooksRegional(
  String text
){

  text=
    normalizeInput(
      text
    );


  const char* clues[]={

    "different countries",

    "varies by country",

    "vary by country",

    "varies according to country",

    "depending on the country",

    "depending on country",

    "different dates in different countries",

    "observed on different dates",

    "celebrated on different dates",

    "various dates",

    "dates vary",

    "country to country"
  };


  for(
    size_t i=0;
    i<
    sizeof(clues)/
    sizeof(clues[0]);
    i++
  ){

    if(
      text.indexOf(
        clues[i]
      )>=0
    ){

      return true;
    }
  }


  return false;
}


// =====================================================
//                MOVABLE EVENT
// =====================================================

bool calendarLooksMovable(
  String text
){

  text=
    normalizeInput(
      text
    );


  const char* clues[]={

    "lunar calendar",

    "lunisolar",

    "hindu calendar",

    "islamic calendar",

    "full moon",

    "movable feast",

    "movable festival",

    "date varies each year",

    "varies each year",

    "gregorian date varies",

    "tithi",

    "lunar month"
  };


  for(
    size_t i=0;
    i<
    sizeof(clues)/
    sizeof(clues[0]);
    i++
  ){

    if(
      text.indexOf(
        clues[i]
      )>=0
    ){

      return true;
    }
  }


  return false;
}


// =====================================================
//        REGIONAL DATE FROM EVENT PAGE SECTION
// =====================================================

bool calendarRegionalDateFromPage(
  const String& page,
  const String& region,
  int& day,
  int& month
){

  String sectionIndex;


  if(
    !calendarFindRegionSection(
      page,
      region,
      sectionIndex
    )
  ){

    return false;
  }


  String sectionText;


  if(
    !calendarFetchWikiSectionByIndex(
      page,
      sectionIndex,
      sectionText
    )
  ){

    return false;
  }


  return
    calendarFindBestDateInText(
      sectionText,
      day,
      month
    );
}


// =====================================================
//          GENERIC WEB SEARCH DATE RESOLVER
// =====================================================

bool calendarSearchDate(
  String eventName,
  String region,
  int year,
  bool requireYear,
  int& day,
  int& month,
  String& source
){

  String query=
    eventName;


  if(region.length()){

    query+=" ";

    query+=region;
  }


  if(requireYear){

    query+=" ";

    query+=
      String(year);
  }


  query+=
    " date";


  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=query"

    "&list=search"

    "&srlimit=12"

    "&srprop=snippet"

    "&format=json"

    "&utf8=1"

    "&srsearch="+

    urlEncode(
      query
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

    return false;
  }


  int position=0;


  int bestScore=-100;

  int bestDay=0;

  int bestMonth=0;

  String bestSource="";


  for(int i=0;i<12;i++){

    String title;

    int afterTitle=0;


    if(
      !calendarJsonStringAfter(
        body,
        "title",
        position,
        title,
        afterTitle
      )
    ){

      break;
    }


    position=
      afterTitle;


    String snippet;

    int afterSnippet=0;


    if(
      !calendarJsonStringAfter(
        body,
        "snippet",
        position,
        snippet,
        afterSnippet
      )
    ){

      continue;
    }


    position=
      afterSnippet;


    snippet=
      calendarHtmlToText(
        snippet
      );


    String combined=

      title+
      " "+
      snippet;


    String normalized=
      normalizeInput(
        combined
      );


    int score=
      calendarEventTitleScore(
        eventName,
        title
      );


    if(region.length()){

      String regionCompact=
        calendarCompact(
          region
        );


      String combinedCompact=
        calendarCompact(
          combined
        );


      if(
        combinedCompact.indexOf(
          regionCompact
        )>=0
      ){

        score+=30;
      }

      else{

        score-=15;
      }
    }


    if(requireYear){

      if(
        normalized.indexOf(
          String(year)
        )>=0
      ){

        score+=30;
      }

      else{

        continue;
      }
    }


    int candidateDay=0;

    int candidateMonth=0;


    if(
      calendarFindBestDateInText(
        combined,
        candidateDay,
        candidateMonth
      )
    ){

      score+=25;
    }

    else{

      continue;
    }


    if(score>bestScore){

      bestScore=score;

      bestDay=candidateDay;

      bestMonth=candidateMonth;

      bestSource=
        title;
    }
  }


  if(
    bestScore<30 ||
    bestDay==0 ||
    bestMonth==0
  ){

    return false;
  }


  day=bestDay;

  month=bestMonth;

  source=
    bestSource;


  return true;
}


// =====================================================
//             OCCURRENCE TENSE FOR A YEAR
// =====================================================
//
// Only use this when the answer refers to a SPECIFIC occurrence
// in a specific Gregorian year. Recurring fixed observances keep
// neutral wording such as "is observed on 15 August".
//
// return:
//   -1 -> requested occurrence is in the past
//    0 -> today
//    1 -> future
// =====================================================

static int calendarOccurrenceRelation(
  int year,
  int month,
  int day
){
  struct tm now;

  if(
    !getLocalTime(
      &now,
      500
    )
  ){
    return 0;
  }

  int currentYear=
    now.tm_year+1900;

  int currentMonth=
    now.tm_mon+1;

  int currentDay=
    now.tm_mday;

  if(year<currentYear) return -1;
  if(year>currentYear) return 1;

  if(month<currentMonth) return -1;
  if(month>currentMonth) return 1;

  if(day<currentDay) return -1;
  if(day>currentDay) return 1;

  return 0;
}


static String calendarSpecificOccurrenceVerb(
  int year,
  int month,
  int day
){
  int relation=
    calendarOccurrenceRelation(
      year,
      month,
      day
    );

  if(relation<0) return " was on ";
  if(relation>0) return " will be on ";

  return " is on ";
}


// =====================================================
//                EVENT -> DATE
// =====================================================

bool calendarWhenIsEvent(
  String question,
  String& answer
){

  int year=
    calendarRequestedYear(
      question
    );


  if(year==0){

    answer=
      "My clock is not synchronized yet, so I can't determine the current year reliably.";

    return true;
  }


  String region=
    calendarExtractRegion(
      question
    );


  String eventName=
    calendarExtractEventName(
      question
    );


  if(eventName.length()<2){

    return false;
  }


  Serial.print(
    "[CALENDAR] Event: "
  );

  Serial.println(
    eventName
  );


  Serial.print(
    "[CALENDAR] Year: "
  );

  Serial.println(
    year
  );


  if(region.length()){

    Serial.print(
      "[CALENDAR] Region: "
    );

    Serial.println(
      region
    );
  }


  // ===================================================
  // FIND GENERAL EVENT PAGE
  // ===================================================

  String page="";

  String intro="";


  bool pageFound=
    calendarFindEventPage(
      eventName,
      page
    );


  if(pageFound){

    calendarFetchEventIntro(
      page,
      intro
    );
  }


  bool regional=
    intro.length() &&
    calendarLooksRegional(
      intro
    );


  bool movable=
    intro.length() &&
    calendarLooksMovable(
      intro
    );


  // ===================================================
  // REGION-SPECIFIC EVENT
  // ===================================================

  if(region.length()){

    // -------------------------------------------------
    // Try country/region section on page first.
    // -------------------------------------------------

    if(pageFound){

      int day=0;

      int month=0;


      if(
        calendarRegionalDateFromPage(
          page,
          region,
          day,
          month
        )
      ){

        answer=

          eventName+

          " in "+

          region+

          " is observed on "+

          String(day)+

          " "+

          calendarMonthName(
            month
          )+

          ".";


        return true;
      }
    }


    // -------------------------------------------------
    // Generic region-aware search.
    // -------------------------------------------------

    int day=0;

    int month=0;

    String source;


    if(
      calendarSearchDate(
        eventName,
        region,
        year,
        movable,
        day,
        month,
        source
      )
    ){

      Serial.print(
        "[SOURCE] "
      );

      Serial.println(
        source
      );


      answer=

        eventName+

        " in "+

        region;


      if(movable){

        answer+=
          " in "+

          String(year);
      }


      if(movable){
        answer+=
          calendarSpecificOccurrenceVerb(
            year,
            month,
            day
          );
      }
      else{
        answer+=
          " is on ";
      }

      answer+=
        String(day)+
        " "+
        calendarMonthName(
          month
        )+
        ".";


      return true;
    }


    answer=

      "I found \""+

      eventName+

      "\", but I couldn't verify a reliable date for "+

      region+

      " in "+

      String(year)+

      ", so I won't guess.";


    return true;
  }


  // ===================================================
  // MULTI-COUNTRY OBSERVANCE WITHOUT REGION
  // ===================================================

  if(regional){

    answer=

      eventName+

      " is observed on different dates depending on the country or region. "

      "Tell me the country you mean, for example: \"when is "+

      eventName+

      " in India?\"";


    return true;
  }


  // ===================================================
  // MOVABLE / LUNAR FESTIVAL
  // ===================================================

  if(movable){

    int day=0;

    int month=0;

    String source;


    if(
      calendarSearchDate(
        eventName,
        "",
        year,
        true,
        day,
        month,
        source
      )
    ){

      Serial.print(
        "[SOURCE] "
      );

      Serial.println(
        source
      );


      answer=

        "In "+

        String(year)+

        ", "+

        eventName+

        calendarSpecificOccurrenceVerb(
          year,
          month,
          day
        )+

        String(day)+

        " "+

        calendarMonthName(
          month
        )+

        ".";


      return true;
    }


    answer=

      eventName+

      " is a movable calendar event. "

      "I couldn't verify its "+

      String(year)+

      " Gregorian date from my current sources, so I won't guess.";


    return true;
  }


  // ===================================================
  // FIXED ANNUAL EVENT
  // ===================================================

  if(intro.length()){

    int day=0;

    int month=0;


    if(
      calendarFindBestDateInText(
        intro,
        day,
        month
      )
    ){

      answer=

        page+

        " is observed on "+

        String(day)+

        " "+

        calendarMonthName(
          month
        )+

        ".";


      return true;
    }
  }


  // ===================================================
  // GENERAL SEARCH FALLBACK
  // ===================================================

  int searchDay=0;

  int searchMonth=0;

  String searchSource;


  if(
    calendarSearchDate(
      eventName,
      "",
      year,
      false,
      searchDay,
      searchMonth,
      searchSource
    )
  ){

    Serial.print(
      "[SOURCE] "
    );

    Serial.println(
      searchSource
    );


    answer=

      eventName+

      " is observed on "+

      String(searchDay)+

      " "+

      calendarMonthName(
        searchMonth
      )+

      ".";


    return true;
  }


  answer=

    "I found \""+

    eventName+

    "\", but I couldn't verify a reliable date, so I won't guess.";


  return true;
}


// =====================================================
//            DATE PAGE TITLE
// =====================================================

String calendarDatePage(
  const ElliCalendarDate& date
){

  return

    calendarMonthName(
      date.month
    )

    +

    " "

    +

    String(
      date.day
    );
}


// =====================================================
//            REGION LINE MATCH
// =====================================================

bool calendarLineMatchesRegion(
  String line,
  String region
){

  if(!region.length()){

    return true;
  }


  String a=
    calendarCompact(
      line
    );


  String b=
    calendarCompact(
      region
    );


  if(
    a.indexOf(b)>=0
  ){

    return true;
  }


  // India / Indian, etc.

  if(b.length()>=4){

    String root=
      b.substring(
        0,
        min(
          5,
          (int)b.length()
        )
      );


    if(
      a.indexOf(
        root
      )>=0
    ){

      return true;
    }
  }


  return false;
}


// =====================================================
//          SELECT OBSERVANCE LINES
// =====================================================

String calendarSelectObservances(
  String section,
  String region,
  int maximum
){

  String selected[12];

  int count=0;


  int position=0;


  // ===================================================
  // REGION MATCH PASS
  // ===================================================

  if(region.length()){

    while(
      position<section.length() &&
      count<maximum
    ){

      int end=
        section.indexOf(
          '\n',
          position
        );


      if(end<0){

        end=
          section.length();
      }


      String line=
        section.substring(
          position,
          end
        );


      position=
        end+1;


      line.trim();


      if(
        line.length()<5 ||
        !calendarLineMatchesRegion(
          line,
          region
        )
      ){

        continue;
      }


      selected[count++]=line;
    }
  }


  // ===================================================
  // GENERAL PASS
  // ===================================================

  if(count==0){

    position=0;


    while(
      position<section.length() &&
      count<maximum
    ){

      int end=
        section.indexOf(
          '\n',
          position
        );


      if(end<0){

        end=
          section.length();
      }


      String line=
        section.substring(
          position,
          end
        );


      position=
        end+1;


      line.trim();


      if(line.length()<5){

        continue;
      }


      selected[count++]=line;
    }
  }


  String result="";


  for(int i=0;i<count;i++){

    if(result.length()){

      result+="; ";
    }


    result+=
      selected[i];
  }


  if(result.length()>1000){

    result=
      result.substring(
        0,
        1000
      );

    result+="...";
  }


  return result;
}


// =====================================================
//         FIXED OBSERVANCES FOR DATE
// =====================================================

bool calendarFixedObservances(
  const ElliCalendarDate& date,
  String region,
  String& result
){

  String section;


  if(
    !calendarFetchWikiSection(
      calendarDatePage(date),
      "Holidays and observances",
      section
    )
  ){

    return false;
  }


  result=
    calendarSelectObservances(
      section,
      region,
      8
    );


  return
    result.length()>0;
}


// =====================================================
//       YEAR-SPECIFIC EVENTS ON A DATE
// =====================================================
//
// Used to catch movable festivals on this year's date.
//
// Example conceptually:
//
// 29 August 2026 festival India
//
// No festival/date is hard-coded.
// =====================================================

bool calendarYearEventsOnDate(
  const ElliCalendarDate& date,
  String region,
  String& result
){

  String query=

    "\""+

    String(date.day)+

    " "+

    calendarMonthName(
      date.month
    )+

    "\" "+

    String(
      date.year
    )+

    " festival holiday observance";


  if(region.length()){

    query+=" ";

    query+=region;
  }


  String url=

    "https://en.wikipedia.org/w/api.php"

    "?action=query"

    "&list=search"

    "&srlimit=10"

    "&srprop=snippet"

    "&format=json"

    "&utf8=1"

    "&srsearch="+

    urlEncode(
      query
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

    return false;
  }


  String matches[5];

  int matchCount=0;

  int position=0;


  for(
    int i=0;
    i<10 &&
    matchCount<5;
    i++
  ){

    String title;

    int afterTitle=0;


    if(
      !calendarJsonStringAfter(
        body,
        "title",
        position,
        title,
        afterTitle
      )
    ){

      break;
    }


    position=
      afterTitle;


    String snippet;

    int afterSnippet=0;


    if(
      !calendarJsonStringAfter(
        body,
        "snippet",
        position,
        snippet,
        afterSnippet
      )
    ){

      continue;
    }


    position=
      afterSnippet;


    snippet=
      calendarHtmlToText(
        snippet
      );


    String combined=

      title+
      " "+
      snippet;


    String normalized=
      normalizeInput(
        combined
      );


    // Require requested year.

    if(
      normalized.indexOf(
        String(date.year)
      )<0
    ){

      continue;
    }


    // Require month.

    String month=
      normalizeInput(
        calendarMonthName(
          date.month
        )
      );


    if(
      normalized.indexOf(
        month
      )<0
    ){

      continue;
    }


    // Prefer region when requested.

    if(
      region.length() &&
      !calendarLineMatchesRegion(
        combined,
        region
      )
    ){

      continue;
    }


    matches[
      matchCount++
    ]=
      title;
  }


  if(matchCount==0){

    return false;
  }


  result="";


  for(int i=0;i<matchCount;i++){

    if(result.length()){

      result+=", ";
    }


    result+=
      matches[i];
  }


  return true;
}


// =====================================================
//             DATE -> OBSERVANCES
// =====================================================

bool calendarObservancesForDate(
  const ElliCalendarDate& date,
  String question,
  String& answer
){

  String region=
    calendarExtractRegion(
      question
    );


  String fixed="";

  String yearly="";


  bool fixedFound=
    calendarFixedObservances(
      date,
      region,
      fixed
    );


  bool yearlyFound=
    calendarYearEventsOnDate(
      date,
      region,
      yearly
    );


  if(
    !fixedFound &&
    !yearlyFound
  ){

    return false;
  }


  answer=

    "For "+

    calendarPrettyDate(
      date
    );


  if(region.length()){

    answer+=
      " in "+

      region;
  }


  answer+=": ";


  if(fixedFound){

    answer+=fixed;
  }


  if(
    fixedFound &&
    yearlyFound
  ){

    answer+=
      ". Year-specific festival references also include: ";
  }


  if(
    !fixedFound &&
    yearlyFound
  ){

    answer+=
      "year-specific festival references include: ";
  }


  if(yearlyFound){

    answer+=yearly;
  }


  return true;
}


// =====================================================
//               HISTORICAL EVENTS
// =====================================================

bool calendarHistoricalEvents(
  const ElliCalendarDate& date,
  String& answer
){

  String section;


  if(
    !calendarFetchWikiSection(
      calendarDatePage(date),
      "Events",
      section
    )
  ){

    return false;
  }


  // ===================================================
  // EXPLICIT YEAR:
  // find that year's line.
  // ===================================================

  if(date.explicitYear){

    String year=
      String(
        date.year
      );


    int position=0;


    while(position<section.length()){

      int end=
        section.indexOf(
          '\n',
          position
        );


      if(end<0){

        end=
          section.length();
      }


      String line=
        section.substring(
          position,
          end
        );


      position=
        end+1;


      line.trim();


      if(
        line.indexOf(
          year
        )>=0
      ){

        answer=

          "On "+

          calendarPrettyDate(
            date
          )+

          ", "+

          line;


        return true;
      }
    }
  }


  String selection=
    calendarSelectObservances(
      section,
      "",
      4
    );


  if(!selection.length()){

    return false;
  }


  answer=

    "Historical events associated with "+

    calendarPrettyDate(
      date
    )+

    " include: "+

    selection;


  return true;
}


// =====================================================
//                MAIN CALENDAR BRAIN
// =====================================================

bool handleCalendarBrain(
  const String& input
){

  String q=
    normalizeInput(
      input
    );


  ElliCalendarDate date=
    calendarParseDate(
      q
    );


  // ===================================================
  // DATE -> WEEKDAY
  // ===================================================

  if(
    date.valid &&
    calendarAsksWeekday(q)
  ){

    Serial.println(
      "[CALENDAR] WEEKDAY"
    );


    elliSay(

      calendarPrettyDate(
        date
      )

      +

      " falls on "

      +

      calendarWeekday(
        date
      )

      +

      "."
    );


    return true;
  }


  // ===================================================
  // DATE -> HISTORY
  // ===================================================

  if(
    date.valid &&
    calendarAsksHistory(q)
  ){

    String answer;


    if(
      calendarHistoricalEvents(
        date,
        answer
      )
    ){

      Serial.println(
        "[CALENDAR] HISTORY"
      );


      Serial.println(
        "[SOURCE] Wikipedia / Events"
      );


      elliSay(
        answer
      );
    }

    else{

      elliSay(
        "I understood the historical-date question, but I couldn't fetch reliable events right now."
      );
    }


    return true;
  }


  // ===================================================
  // DATE -> FESTIVAL / OBSERVANCE
  // ===================================================

  if(
    date.valid &&
    calendarAsksObservance(q)
  ){

    String answer;


    if(
      calendarObservancesForDate(
        date,
        q,
        answer
      )
    ){

      Serial.println(
        "[CALENDAR] OBSERVANCES / FESTIVALS"
      );


      Serial.println(
        "[SOURCE] Wikipedia calendar + year-specific search"
      );


      elliSay(
        answer
      );
    }

    else{

      elliSay(

        "I recognized the date as "+

        calendarPrettyDate(
          date
        )+

        ", but I couldn't verify a reliable observance or festival from my current sources."
      );
    }


    return true;
  }


  // ===================================================
  // EVENT -> DATE
  // ===================================================

  if(
    calendarAsksEventDate(q)
  ){

    String answer;


    if(
      calendarWhenIsEvent(
        q,
        answer
      )
    ){

      Serial.println(
        "[CALENDAR] EVENT -> DATE"
      );


      elliSay(
        answer
      );


      return true;
    }
  }


  return false;
}