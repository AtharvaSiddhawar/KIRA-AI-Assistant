#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "kira_school.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

enum SubjectId : uint8_t {
  SUB_NONE=0, SUB_ALG, SUB_SCI, SUB_GEM, SUB_HIST, SUB_ENG, SUB_HINDI,
  SUB_MARA, SUB_GAME, SUB_WS, SUB_ELAB, SUB_DS, SUB_GEOG, SUB_HPE, SUB_VES
};

struct SubjectInfo { SubjectId id; const char* code; const char* full; bool bag; };
static const SubjectInfo SUBJECTS[]={
  {SUB_NONE,"","",false},
  {SUB_ALG,"Alg","Algebra",true},
  {SUB_SCI,"Sci","Science",true},
  {SUB_GEM,"Gem","Geometry",true},
  {SUB_HIST,"Hist","History",true},
  {SUB_ENG,"Eng","English",true},
  {SUB_HINDI,"Hindi","Hindi",true},
  {SUB_MARA,"Mara","Marathi",true},
  {SUB_GAME,"Game","Game",true},
  {SUB_WS,"WS","Water Security",true},
  {SUB_ELAB,"ELab","English Lab",true},
  {SUB_DS,"DS","Defence Studies",true},
  {SUB_GEOG,"Geog","Geography",true},
  {SUB_HPE,"HPE","Health and Physical Education",true},
  {SUB_VES,"VES","Off period",false}
};

// Monday..Saturday, periods 1..10. VES and blank periods are never bag subjects.
static const SubjectId TABLE[6][10]={
  {SUB_ALG,SUB_SCI,SUB_GEM,SUB_HIST,SUB_HIST,SUB_ENG,SUB_HINDI,SUB_HINDI,SUB_GAME,SUB_WS},
  {SUB_ALG,SUB_SCI,SUB_GEM,SUB_HIST,SUB_ENG,SUB_ENG,SUB_HINDI,SUB_MARA,SUB_GAME,SUB_WS},
  {SUB_ALG,SUB_SCI,SUB_GEM,SUB_SCI,SUB_ENG,SUB_MARA,SUB_HIST,SUB_ELAB,SUB_HINDI,SUB_DS},
  {SUB_ALG,SUB_SCI,SUB_SCI,SUB_GEOG,SUB_ENG,SUB_MARA,SUB_GEOG,SUB_ELAB,SUB_HINDI,SUB_HPE},
  {SUB_ALG,SUB_SCI,SUB_SCI,SUB_GEOG,SUB_ENG,SUB_MARA,SUB_GEOG,SUB_MARA,SUB_HPE,SUB_DS},
  {SUB_GEM,SUB_SCI,SUB_SCI,SUB_GEOG,SUB_ENG,SUB_VES,SUB_NONE,SUB_NONE,SUB_NONE,SUB_NONE}
};

static const uint16_t PERIOD_START[10]={445,485,515,565,595,625,655,695,725,755};
static const uint16_t PERIOD_END[10]  ={485,515,545,595,625,655,685,725,755,780};
static const char* DAY_NAMES[6]={"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};

Preferences prefs;
bool prefsReady=false;
bool reminderEnabled=true;
uint16_t reminderMinute=19*60; // 7 PM default; configurable.
uint16_t bagCutoffMinute=12*60; // User rule: before 12 PM -> next school day; from 12 PM -> current day.
uint32_t lastReminderKey=0;

bool bagActive=false;
uint8_t bagDay=0;
SubjectId bagSubjects[10];
uint8_t bagCount=0;
uint16_t bagDoneMask=0;
uint16_t bagMentionedMask=0;
uint8_t bagCurrent=0;

bool manualClock=false;
uint8_t manualDay=0;
uint16_t manualStartMinute=8*60;
uint32_t manualStartMs=0;

const SubjectInfo& info(SubjectId id){
  for(size_t i=0;i<sizeof(SUBJECTS)/sizeof(SUBJECTS[0]);i++) if(SUBJECTS[i].id==id) return SUBJECTS[i];
  return SUBJECTS[0];
}

String lower(String s){ s.toLowerCase(); s.trim(); return s; }

int dayFromText(const String& q){
  String n=lower(q);
  const char* keys[]={"monday","tuesday","wednesday","thursday","friday","saturday","sunday"};
  for(int i=0;i<7;i++) if(n.indexOf(keys[i])>=0) return i<6 ? i : 6;
  return -1;
}

bool actualClock(uint8_t& day,uint16_t& minute,uint32_t& dayKey){
  struct tm t;
  if(getLocalTime(&t,0) && t.tm_year+1900>=2024){
    if(t.tm_wday==0) day=6; // Sunday sentinel
    else day=(uint8_t)(t.tm_wday-1);
    minute=(uint16_t)(t.tm_hour*60+t.tm_min);
    dayKey=(uint32_t)(t.tm_year+1900)*400UL+(uint32_t)t.tm_yday+1UL;
    return true;
  }
  if(manualClock){
    uint32_t elapsedMin=(millis()-manualStartMs)/60000UL;
    uint32_t total=(uint32_t)manualStartMinute+elapsedMin;
    uint32_t addDays=total/1440UL;
    minute=(uint16_t)(total%1440UL);
    day=(uint8_t)((manualDay+addDays)%7);
    dayKey=0;
    return true;
  }
  return false;
}

uint8_t nextSchoolDay(uint8_t d){
  // 0..5 Mon-Sat, 6 Sunday. Sunday always rolls to Monday.
  for(uint8_t i=1;i<=7;i++){
    uint8_t n=(d+i)%7;
    if(n<=5) return n;
  }
  return 0;
}

String dayName(uint8_t d){ return d<=5 ? String(DAY_NAMES[d]) : String("Sunday"); }

String timeText(uint16_t m){
  int h=m/60, min=m%60;
  bool pm=h>=12;
  int h12=h%12; if(h12==0) h12=12;
  char b[20]; snprintf(b,sizeof(b),"%d:%02d %s",h12,min,pm?"PM":"AM");
  return String(b);
}

bool parseTimeText(String s,uint16_t& out){
  s=normalizeInput(s);
  bool pm=s.indexOf(" pm")>=0 || s.endsWith("pm");
  bool am=s.indexOf(" am")>=0 || s.endsWith("am");
  int pos=-1;
  for(int i=0;i<(int)s.length();i++) if(isdigit((unsigned char)s[i])){pos=i;break;}
  if(pos<0) return false;
  int end=pos;
  while(end<(int)s.length() && (isdigit((unsigned char)s[end]) || s[end]==':')) end++;
  String tok=s.substring(pos,end);
  int colon=tok.indexOf(':');
  int h=0,m=0;
  if(colon>=0){ h=tok.substring(0,colon).toInt(); m=tok.substring(colon+1).toInt(); }
  else h=tok.toInt();
  if(am||pm){
    if(h<1||h>12||m>59) return false;
    if(h==12) h=0;
    if(pm) h+=12;
  }else if(h>23||m>59) return false;
  out=(uint16_t)(h*60+m);
  return true;
}

int ordinalPeriod(String q){
  q=normalizeInput(q);
  const char* words[]={"first","second","third","fourth","fifth","sixth","seventh","eighth","ninth","tenth"};
  for(int i=0;i<10;i++) if(q.indexOf(String(words[i])+" period")>=0 || q.indexOf("period "+String(words[i]))>=0) return i;
  for(int i=1;i<=10;i++){
    if(q.indexOf("period "+String(i))>=0 || q.indexOf(String(i)+" period")>=0) return i-1;
  }
  return -1;
}

void printDay(uint8_t d){
  Serial.println();
  Serial.print("========== "); Serial.print(dayName(d)); Serial.println(" TIMETABLE ==========");
  if(d>5){ Serial.println("No school timetable on Sunday."); Serial.println("===================================="); return; }
  for(int p=0;p<10;p++){
    SubjectId s=TABLE[d][p];
    if(s==SUB_NONE) continue;
    Serial.print(p+1); Serial.print(". ");
    Serial.print(timeText(PERIOD_START[p])); Serial.print(" - "); Serial.print(timeText(PERIOD_END[p]));
    Serial.print(" | "); Serial.print(info(s).code); Serial.print(" | "); Serial.println(info(s).full);
  }
  Serial.println("====================================");
}

uint8_t buildBagList(uint8_t d,SubjectId* out){
  if(d>5) return 0;
  uint8_t count=0;
  for(int p=0;p<10;p++){
    SubjectId s=TABLE[d][p];
    if(s==SUB_NONE || !info(s).bag) continue;
    bool seen=false;
    for(uint8_t i=0;i<count;i++) if(out[i]==s){seen=true;break;}
    if(!seen && count<10) out[count++]=s;
  }
  return count;
}

uint8_t resolveRequestedDay(String q,bool forBag,bool& clockOk){
  int explicitDay=dayFromText(q);
  if(explicitDay>=0){ clockOk=true; return explicitDay==6 ? (forBag?nextSchoolDay(6):6) : (uint8_t)explicitDay; }

  uint8_t currentDay; uint16_t minute; uint32_t key;
  clockOk=actualClock(currentDay,minute,key);
  if(!clockOk) return 0;

  if(q.indexOf("tomorrow")>=0) return nextSchoolDay(currentDay);
  if(q.indexOf("today")>=0 && !forBag) return currentDay;

  if(forBag){
    uint8_t target=minute<bagCutoffMinute ? nextSchoolDay(currentDay) : currentDay;
    if(target>5 || buildBagList(target,bagSubjects)==0) target=nextSchoolDay(target);
    return target;
  }
  return currentDay;
}

void speakPeriod(uint8_t d,int p){
  if(d>5){ elliSay("There is no school timetable on Sunday."); return; }
  if(p<0||p>=10||TABLE[d][p]==SUB_NONE){ elliSay("There is no class in that period on "+dayName(d)+"."); return; }
  SubjectId s=TABLE[d][p];
  String r="Period "+String(p+1)+" on "+dayName(d)+" is "+String(info(s).full);
  if(s==SUB_VES) r+=" — that is your off period";
  r+=".";
  elliSay(r);
}

void speakTimetable(uint8_t d){
  printDay(d);
  if(d>5){ elliSay("There are no classes on Sunday. The next school day is Monday."); return; }
  String r=dayName(d)+" periods are: ";
  bool first=true;
  for(int p=0;p<10;p++){
    SubjectId s=TABLE[d][p]; if(s==SUB_NONE) continue;
    if(!first) r+=", ";
    r+=info(s).full; first=false;
  }
  r+=". I printed the period times in Serial Monitor.";
  elliSay(r);
}

int currentPeriodIndex(uint16_t minute){
  for(int i=0;i<10;i++) if(minute>=PERIOD_START[i] && minute<PERIOD_END[i]) return i;
  return -1;
}

int nextPeriodIndex(uint16_t minute,uint8_t d){
  if(d>5) return -1;
  for(int i=0;i<10;i++) if(TABLE[d][i]!=SUB_NONE && PERIOD_START[i]>minute) return i;
  return -1;
}

void persistSettings(){
  if(!prefsReady) return;
  prefs.putBool("rem_on",reminderEnabled);
  prefs.putUShort("rem_min",reminderMinute);
  prefs.putUShort("cut_min",bagCutoffMinute);
}

void announceBagCurrent(){
  while(bagCurrent<bagCount && (bagDoneMask&(1U<<bagCurrent))) bagCurrent++;
  if(bagCurrent>=bagCount){
    bagActive=false;
    elliSay("Your bag checklist for "+dayName(bagDay)+" is complete. Every required subject was listed only once.");
    return;
  }
  bagMentionedMask|=(1U<<bagCurrent);
  elliSay("Next for "+dayName(bagDay)+": "+String(info(bagSubjects[bagCurrent]).full)+". Say 'done' when it is packed.");
}

void startBag(String q){
  bool ok=false;
  uint8_t d=resolveRequestedDay(q,true,ok);
  if(!ok && dayFromText(q)<0){
    elliSay("I need a valid clock to choose the bag day automatically. Connect once for time sync, or say 'fill my bag for Monday'.");
    return;
  }
  if(d>5) d=nextSchoolDay(d);
  bagDay=d;
  bagCount=buildBagList(d,bagSubjects);
  bagDoneMask=0; bagMentionedMask=0; bagCurrent=0; bagActive=bagCount>0;
  if(!bagActive){ elliSay("I couldn't build a bag list for that day."); return; }
  Serial.println(); Serial.print("[BAG] Unique subjects for "); Serial.print(dayName(d)); Serial.print(": ");
  for(uint8_t i=0;i<bagCount;i++){ if(i) Serial.print(", "); Serial.print(info(bagSubjects[i]).full); }
  Serial.println();
  elliSay("Bag checklist started for "+dayName(d)+" with "+String(bagCount)+" unique subjects. I will not repeat duplicate timetable subjects.");
  announceBagCurrent();
}

void bagStatus(){
  if(!bagActive){ elliSay("There is no active bag checklist. Say 'fill my bag' to start one."); return; }
  uint8_t done=0; for(uint8_t i=0;i<bagCount;i++) if(bagDoneMask&(1U<<i)) done++;
  String r="Bag for "+dayName(bagDay)+": "+String(done)+" of "+String(bagCount)+" subjects packed.";
  if(bagCurrent<bagCount) r+=" Current: "+String(info(bagSubjects[bagCurrent]).full)+".";
  elliSay(r);
}

int subjectFromText(String q){
  q=normalizeInput(q);
  for(size_t i=1;i<sizeof(SUBJECTS)/sizeof(SUBJECTS[0]);i++){
    String code=normalizeInput(SUBJECTS[i].code), full=normalizeInput(SUBJECTS[i].full);
    if((code.length()>2 && q.indexOf(code)>=0) || (full.length() && q.indexOf(full)>=0)) return SUBJECTS[i].id;
  }
  return SUB_NONE;
}

bool handleDone(String q){
  if(!bagActive) return false;
  if(q=="done" || q=="packed" || q=="done with it" || q=="i packed it"){
    if(bagCurrent<bagCount) bagDoneMask|=(1U<<bagCurrent);
    bagCurrent++;
    announceBagCurrent();
    return true;
  }
  if(q.startsWith("done ") || q.startsWith("packed ")){
    int sid=subjectFromText(q);
    if(sid==SUB_NONE){ elliSay("I couldn't match that to a subject in the active bag list."); return true; }
    for(uint8_t i=0;i<bagCount;i++){
      if(bagSubjects[i]==sid){
        bagDoneMask|=(1U<<i);
        if(i==bagCurrent) bagCurrent++;
        elliSay(String(info((SubjectId)sid).full)+" marked packed.");
        announceBagCurrent();
        return true;
      }
    }
    elliSay("That subject is not needed for the active bag day.");
    return true;
  }
  return false;
}

} // namespace

void kiraSchoolBegin(){
  prefsReady=prefs.begin("kira_school",false);
  if(prefsReady){
    reminderEnabled=prefs.getBool("rem_on",true);
    reminderMinute=prefs.getUShort("rem_min",19*60);
    bagCutoffMinute=prefs.getUShort("cut_min",12*60);
    lastReminderKey=prefs.getUInt("last_rem",0);
  }
  Serial.println("[SCHOOL V1.8] Class X timetable + unique-subject bag assistant ready.");
  Serial.print("[SCHOOL V1.8] Bag cutoff: "); Serial.println(timeText(bagCutoffMinute));
  Serial.print("[SCHOOL V1.8] Bag reminder: "); Serial.print(reminderEnabled?"ON at ":"OFF at "); Serial.println(timeText(reminderMinute));
}

void kiraSchoolTick(){
  if(!reminderEnabled) return;
  uint8_t d; uint16_t minute; uint32_t key;
  if(!actualClock(d,minute,key) || !key) return;
  if(d==6) return;
  if(minute<reminderMinute) return;
  if(lastReminderKey==key) return;
  lastReminderKey=key;
  if(prefsReady) prefs.putUInt("last_rem",lastReminderKey);
  Serial.println("[BAG REMINDER] Time to prepare your school bag.");
  elliSay("Bag reminder: remember to prepare your school bag. Say 'fill my bag' or name a day explicitly.");
}

bool kiraSchoolBagActive(){ return bagActive; }
uint8_t kiraSchoolUniqueBagCountForDay(uint8_t d){ SubjectId temp[10]; return buildBagList(d,temp); }

String kiraSchoolSubjectFullName(const String& code){
  String n=normalizeInput(code);
  for(size_t i=1;i<sizeof(SUBJECTS)/sizeof(SUBJECTS[0]);i++) if(normalizeInput(SUBJECTS[i].code)==n) return SUBJECTS[i].full;
  return code;
}

String kiraSchoolStatus(){
  String r="School timetable ready. Bag cutoff "+timeText(bagCutoffMinute)+", reminder ";
  r+=reminderEnabled ? "on at "+timeText(reminderMinute) : "off";
  r+=". VES is treated as an off period and excluded from bag lists.";
  return r;
}

bool kiraSchoolLooksLikeCommand(const String& original){
  String q=normalizeInput(original);
  if(bagActive && (q=="done" || q=="packed" || q=="done with it" || q=="i packed it" || q.startsWith("done ") || q.startsWith("packed "))) return true;
  return q.indexOf("timetable")>=0 || q.indexOf("period")>=0 || q.indexOf("my bag")>=0 || q.indexOf("bag reminder")>=0 || q.indexOf("bag cutoff")>=0 ||
    q=="bag status" || q=="continue bag" || q=="continue my bag" || q=="cancel bag" || q=="restart bag" || q=="school status" ||
    q.startsWith("set school day ") || q.startsWith("set school time ") || q.startsWith("set school clock ");
}

bool kiraSchoolHandleCommand(String q){
  q=normalizeInput(q);
  if(handleDone(q)) return true;

  if(q=="school status"){
    elliSay(kiraSchoolStatus()); return true;
  }
  if(q.startsWith("set school day ")){
    int d=dayFromText(q);
    if(d<0){ elliSay("Name a weekday, for example 'set school day Monday'."); return true; }
    manualClock=true; manualDay=(uint8_t)d; manualStartMs=millis();
    elliSay("Manual school day set to "+dayName(manualDay)+" for offline timetable testing."); return true;
  }
  if(q.startsWith("set school time ")){
    uint16_t m;
    if(!parseTimeText(q,m)){ elliSay("Try a time like 'set school time 10:30 AM'."); return true; }
    manualClock=true; manualStartMinute=m; manualStartMs=millis();
    elliSay("Manual school time set to "+timeText(m)+" for offline timetable testing."); return true;
  }
  if(q.startsWith("set school clock ")){
    int d=dayFromText(q); uint16_t m;
    if(d<0 || !parseTimeText(q,m)){ elliSay("Try 'set school clock Monday 10:30 AM'."); return true; }
    manualClock=true; manualDay=(uint8_t)d; manualStartMinute=m; manualStartMs=millis();
    elliSay("Manual school clock set to "+dayName(manualDay)+" "+timeText(m)+"."); return true;
  }

  if(q=="bag reminder on"){
    reminderEnabled=true; persistSettings(); elliSay("Bag reminder is on for "+timeText(reminderMinute)+"."); return true;
  }
  if(q=="bag reminder off"){
    reminderEnabled=false; persistSettings(); elliSay("Bag reminder is off."); return true;
  }
  if(q=="bag reminder status"){
    elliSay(String("Bag reminder is ")+(reminderEnabled?"on at "+timeText(reminderMinute):"off")+"."); return true;
  }
  if(q.startsWith("set bag reminder for ") || q.startsWith("bag reminder at ")){
    uint16_t m;
    if(!parseTimeText(q,m)){ elliSay("Try 'set bag reminder for 7 PM'."); return true; }
    reminderMinute=m; reminderEnabled=true; persistSettings();
    elliSay("Bag reminder set for "+timeText(m)+"."); return true;
  }
  if(q.startsWith("set bag cutoff ") || q.startsWith("bag cutoff at ")){
    uint16_t m;
    if(!parseTimeText(q,m)){ elliSay("Try 'set bag cutoff 12 PM' or 'set bag cutoff 12 AM'."); return true; }
    bagCutoffMinute=m; persistSettings();
    elliSay("Bag day cutoff set to "+timeText(m)+". Before that time I choose the next school day; from that time onward I choose the current day."); return true;
  }
  if(q=="bag cutoff" || q=="bag cutoff status"){
    elliSay("Bag cutoff is "+timeText(bagCutoffMinute)+"."); return true;
  }

  if(q=="bag status") { bagStatus(); return true; }
  if(q=="continue bag" || q=="continue my bag" || q=="what is next" || q=="next bag subject"){
    if(!bagActive) elliSay("There is no active bag checklist. Say 'fill my bag' to start."); else announceBagCurrent();
    return true;
  }
  if(q=="cancel bag" || q=="stop bag" || q=="cancel my bag"){
    bagActive=false; bagCount=0; bagDoneMask=0; bagMentionedMask=0; elliSay("Bag checklist cancelled."); return true;
  }
  if(q=="restart bag"){
    if(bagCount && bagDay<=5) startBag("fill my bag for "+dayName(bagDay)); else startBag("fill my bag"); return true;
  }
  if(q.indexOf("fill my bag")>=0 || q.indexOf("pack my bag")>=0 || q=="start bag" || q=="start bag checklist"){
    startBag(q); return true;
  }

  int p=ordinalPeriod(q);
  if(p>=0){
    bool ok=false; uint8_t d=resolveRequestedDay(q,false,ok);
    if(!ok && dayFromText(q)<0){ elliSay("I need the day. Say for example 'third period on Wednesday'."); return true; }
    speakPeriod(d,p); return true;
  }

  if(q=="what period is it now" || q=="what is my current period" || q=="current period"){
    uint8_t d; uint16_t min; uint32_t key;
    if(!actualClock(d,min,key)){ elliSay("I don't have a valid school clock yet. Connect for time sync or set the school clock manually."); return true; }
    if(d>5){ elliSay("There are no classes on Sunday."); return true; }
    int cp=currentPeriodIndex(min);
    if(cp>=0 && TABLE[d][cp]!=SUB_NONE) speakPeriod(d,cp);
    else elliSay("You are not inside a scheduled class period right now.");
    return true;
  }

  if(q=="what is my next period" || q=="what is next period" || q=="next period"){
    uint8_t d; uint16_t min; uint32_t key;
    if(!actualClock(d,min,key)){ elliSay("I don't have a valid school clock yet."); return true; }
    int np=nextPeriodIndex(min,d);
    if(np>=0) speakPeriod(d,np); else elliSay("There is no later class period in today's timetable.");
    return true;
  }

  if(q.indexOf("timetable")>=0 || q.indexOf("my periods")>=0 || q.indexOf("periods today")>=0 || q.indexOf("periods tomorrow")>=0){
    bool ok=false; uint8_t d=resolveRequestedDay(q,false,ok);
    if(!ok && dayFromText(q)<0){ elliSay("I need a valid day. Say 'timetable Monday' if the clock is offline."); return true; }
    speakTimetable(d); return true;
  }

  return false;
}

bool kiraSchoolTimetableSelfTest(){
  SubjectId temp[10];
  bool monday=buildBagList(0,temp)==8;
  bool saturday=buildBagList(5,temp)==4; // VES excluded.
  bool mappings=String(info(SUB_WS).full)=="Water Security" && String(info(SUB_DS).full)=="Defence Studies" && String(info(SUB_GEM).full)=="Geometry";
  return monday && saturday && mappings && TABLE[2][3]==SUB_SCI && TABLE[4][9]==SUB_DS;
}
