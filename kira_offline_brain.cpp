#include <Arduino.h>
#include <Preferences.h>
#include "kira_offline_brain.h"
#include "kira_network_v2.h"
#include "kira_v1_router.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

struct KnowledgeEntry { const char* title; const char* aliases; const char* answer; };

static const KnowledgeEntry PACK[]={
  {"KIRA","kira|kinetic interactive reactive assistant|elli assistant",
   "KIRA stands for Kinetic Interactive Reactive Assistant. It is an ESP32-S3 based assistant architecture that combines local tools, memory, verified online knowledge, conversation context, study features, and future voice/display hardware. Elli is KIRA's assistant character."},
  {"ESP32-S3","esp32 s3|esp32-s3|espressif s3",
   "The ESP32-S3 is a dual-core 32-bit microcontroller from Espressif with Wi-Fi and Bluetooth Low Energy. It supports vector instructions useful for signal processing and AI-style workloads, and boards can include external PSRAM and large flash."},
  {"Microcontroller","microcontroller|mcu",
   "A microcontroller is a small computer on one integrated circuit. It combines a processor, memory, and hardware peripherals so it can read sensors, control devices, communicate, and run embedded programs."},
  {"Arduino","arduino|arduino ide|sketch",
   "Arduino is an embedded-development ecosystem with easy-to-use boards, libraries, and an IDE. An Arduino sketch normally uses setup() for initialization and loop() for repeated work, while .h and .cpp files can organize larger C++ projects."},
  {"Artificial intelligence","artificial intelligence|ai|machine intelligence",
   "Artificial intelligence is the field of building computer systems that perform tasks associated with intelligent behavior, such as perception, language, reasoning, prediction, and decision support. AI includes many methods, not only large language models."},
  {"Neural network","neural network|artificial neural network|ann",
   "A neural network is a computational model made from connected processing units arranged in layers or other structures. During training, adjustable parameters are changed so the network learns patterns from data."},
  {"Neuromorphic computing","neuromorphic computing|neuromorphic engineering|spiking computing",
   "Neuromorphic computing designs hardware and algorithms inspired by nervous systems. Many neuromorphic systems use event-driven communication and spiking neural networks so computation can happen only when events occur, potentially reducing energy use for suitable workloads."},
  {"Spiking neural network","spiking neural network|snn|spiking neurons",
   "A spiking neural network represents information using discrete spike events and their timing. Neurons integrate incoming signals and emit spikes when their internal state crosses a threshold, making SNNs a natural match for event-driven neuromorphic hardware."},
  {"Wi-Fi","wifi|wi fi|wireless lan|wlan",
   "Wi-Fi is a family of wireless networking technologies based on IEEE 802.11 standards. It lets devices communicate through radio links, usually through an access point or router, to reach a local network and often the internet."},
  {"Bluetooth","bluetooth|ble|bluetooth low energy",
   "Bluetooth is a short-range wireless technology for exchanging data between nearby devices. Bluetooth Low Energy is optimized for low-power devices and small bursts of data, while classic Bluetooth supports use cases such as continuous audio."},
  {"PSRAM","psram|pseudo static ram",
   "PSRAM is external volatile memory that behaves similarly to RAM from the program's point of view but uses a denser internal design. On an ESP32-S3 it is useful for large buffers, images, audio data, history, and temporary AI or networking workspaces."},
  {"Flash memory","flash memory|flash vs ram|program flash",
   "Flash is non-volatile memory, so it keeps data without power. ESP32 firmware and persistent files can live in flash. RAM and PSRAM are much faster working memory but normally lose their contents when power is removed."},
  {"NVS","nvs|non volatile storage|preferences",
   "ESP32 NVS is a key-value storage system kept in flash. The Arduino Preferences library uses NVS and is useful for small persistent settings, counters, memories, and configuration that must survive reset or power loss."},
  {"I2S","i2s|inter ic sound|digital audio bus",
   "I2S is a digital audio interface used to move sampled audio between chips. Signals commonly include a bit clock, word-select clock, and data line. KIRA can later use I2S for a digital microphone or an amplifier such as the MAX98357A."},
  {"Sensor","sensor|sensors",
   "A sensor measures a physical property and converts it into a signal a computer can process. Examples include temperature, light, distance, acceleration, sound, and pressure sensors."},
  {"Actuator","actuator|actuators",
   "An actuator converts an electrical control signal into a physical effect. Motors, relays, solenoids, speakers, and some lights are examples of actuators."},
  {"Relay","relay|relay module",
   "A relay is an electrically controlled switch. A low-voltage control circuit can command another circuit to open or close. When mains electricity is involved, isolation, ratings, enclosures, and adult-qualified electrical work are essential."},
  {"Voltage","voltage|potential difference",
   "Voltage is electric potential difference: energy transferred per unit charge between two points. Its SI unit is the volt."},
  {"Electric current","electric current|current|ampere",
   "Electric current is the rate at which electric charge flows. Its SI unit is the ampere, where one ampere corresponds to one coulomb of charge passing a point each second."},
  {"Resistance","resistance|electrical resistance|ohm",
   "Electrical resistance describes how strongly a component opposes current. Its SI unit is the ohm. For an ohmic component, resistance relates voltage and current through Ohm's law."},
  {"Ohm's law","ohms law|ohm law|v equals ir",
   "Ohm's law states V = I × R for an ohmic conductor under appropriate conditions, where V is voltage, I is current, and R is resistance."},
  {"Electric power","electric power|electrical power|power formula",
   "Electrical power is the rate of electrical energy transfer. A common relation is P = V × I, where power P is in watts, voltage V in volts, and current I in amperes."},
  {"Algebra","algebra|algebraic expression|equation",
   "Algebra uses symbols to represent numbers and relationships. It lets us form expressions and equations, apply the same valid operation to both sides of an equation, and solve for unknown quantities."},
  {"Geometry","geometry|geometric shapes",
   "Geometry studies shapes, sizes, positions, angles, lengths, areas, and spatial relationships. It includes topics such as triangles, circles, coordinate geometry, similarity, and mensuration."},
  {"Computer network","computer network|networking|network",
   "A computer network is a group of connected devices that exchange data using agreed communication rules called protocols. Networks can be local, such as a LAN, or span large distances, such as the internet."},
  {"Internet","internet|the internet",
   "The internet is a global system of interconnected networks that exchange packets using the Internet Protocol suite. Services such as the web, email, messaging, and cloud APIs operate on top of this network infrastructure."},
  {"API","api|application programming interface",
   "An API is a defined interface that lets one software system request data or actions from another. Web APIs often exchange structured data such as JSON over HTTP or HTTPS."},
  {"JSON","json|javascript object notation",
   "JSON is a text format for structured data using objects, arrays, strings, numbers, booleans, and null. Web APIs commonly use JSON because it is compact and easy for programs to parse."}
};
static const uint8_t PACK_COUNT=sizeof(PACK)/sizeof(PACK[0]);

struct QuizEntry { const char* topic; const char* q; const char* a; const char* b; const char* c; const char* d; const char* correct; const char* why; };
static const QuizEntry QUIZ[]={
  {"esp32","Which feature is built into the ESP32-S3?","Wi-Fi","A mechanical relay","A mains transformer","A hard disk","a","The ESP32-S3 includes Wi-Fi hardware on the chip."},
  {"esp32","What is PSRAM mainly useful for in an ESP32 project?","Increasing GPIO voltage","Providing larger working-memory buffers","Replacing every flash write","Generating mains power","b","PSRAM provides extra volatile working memory for large buffers and data."},
  {"ai","Which statement best describes artificial intelligence?","Only humanoid robots","Systems designed for tasks such as perception, language, reasoning or prediction","Any program containing an if statement","Only large language models","b","AI is a broad field and includes many methods beyond LLMs."},
  {"neuromorphic","What often distinguishes neuromorphic systems?","They must use mechanical switches","They are inspired by nervous-system computation and may be event-driven","They cannot use digital electronics","They require internet access","b","Neuromorphic designs commonly borrow event-driven ideas from biological nervous systems."},
  {"neuromorphic","What does SNN stand for?","Serial Network Node","Spiking Neural Network","Static Number Network","Signal Normalization Network","b","SNN means Spiking Neural Network."},
  {"electricity","Which unit measures electric current?","Volt","Watt","Ampere","Ohm","c","Electric current is measured in amperes."},
  {"electricity","Which equation is Ohm's law?","V = I × R","P = m × g","E = m × c","v = d + t","a","Ohm's law relates voltage, current and resistance as V = I × R."},
  {"electricity","If V = 12 V and I = 2 A, what is electrical power?","6 W","10 W","14 W","24 W","d","P = V × I = 12 × 2 = 24 watts."},
  {"networking","What is Wi-Fi primarily used for?","Wireless data networking","Measuring temperature","Mechanical switching","Storing files without memory","a","Wi-Fi provides wireless networking based on IEEE 802.11 technologies."},
  {"networking","What is an API?","A battery chemistry","A defined software interface for requests and responses","A type of resistor","A display connector","b","An API defines how software systems interact."},
  {"arduino","Which Arduino function normally runs once at startup?","loop()","setup()","repeat()","mainTask()","b","setup() normally performs one-time initialization."},
  {"arduino","Which file extension is commonly used for an Arduino sketch?",".ino",".jpg",".mp3",".exe only","a","Arduino sketch tabs use the .ino extension."},
  {"algebra","If 3x = 15, what is x?","3","5","12","45","b","Divide both sides by 3 to get x = 5."},
  {"geometry","How many degrees are in the interior angles of a triangle combined?","90","180","270","360","b","A triangle's interior angles sum to 180 degrees."},
  {"kira","What is Elli in the KIRA project?","The assistant character/interface","A Wi-Fi standard","A resistor value","An operating system","a","Elli is KIRA's assistant character/interface."}
};
static const uint8_t QUIZ_COUNT=sizeof(QUIZ)/sizeof(QUIZ[0]);

Preferences prefs;
bool prefsReady=false;
const uint8_t MAX_LEARNED=8;
const uint8_t MAX_PENDING=6;
uint8_t learnedHead=0;
bool internetWasAvailable=false;

String norm(String s){
  s=normalizeInput(s); s.toLowerCase();
  String o; bool space=false;
  for(size_t i=0;i<s.length();i++){
    char c=s[i];
    if(isalnum((unsigned char)c)){o+=c;space=false;}
    else if(o.length()&&!space){o+=' ';space=true;}
  }
  o.trim(); while(o.indexOf("  ")>=0)o.replace("  "," "); return o;
}

bool wordPresent(const String& hay,const String& word){
  if(!word.length()) return false;
  String h=" "+hay+" ", w=" "+word+" "; return h.indexOf(w)>=0;
}

bool topicStopWord(const String& w){
  // Question scaffolding must never be treated as the subject of a
  // knowledge match. This specifically prevents unrelated learned items
  // such as "who invented microcomputer" from matching "who invented zero".
  const char* stop[]={
    "what","who","where","when","which","why","how",
    "is","are","was","were","the","a","an","of","about",
    "explain","tell","me","can","could","would","you","please",
    "more","and","in","to","for","does","do","did","define",
    "invent","invented","inventor","create","created","discover",
    "discovered","located","location","born","called","meaning"
  };
  for(size_t i=0;i<sizeof(stop)/sizeof(stop[0]);i++){
    if(w==stop[i]) return true;
  }
  return false;
}

bool topicWordPresent(const String& text,const String& word){
  if(wordPresent(text,word)) return true;

  // Tiny morphology helper for embedded knowledge topics:
  // computer/computing/computers, network/networking, etc.
  if(word.length()>=6){
    String stem=word.substring(0,6);
    int start=0;
    while(start<(int)text.length()){
      int end=text.indexOf(' ',start); if(end<0) end=text.length();
      String t=text.substring(start,end); start=end+1;
      if(t.length()>=6 && t.substring(0,6)==stem) return true;
    }
  }
  return false;
}

int tokenScore(String query,String text){
  query=norm(query); text=norm(text);
  if(!query.length()||!text.length()) return 0;
  if(query==text) return 100;

  // Exact topic/alias containment is still a strong match, but only after
  // normalization. The generic question wording is handled below.
  if(query.indexOf(text)>=0 || text.indexOf(query)>=0) return 92;

  int total=0,hit=0;
  int start=0;
  while(start<(int)query.length()){
    int end=query.indexOf(' ',start); if(end<0) end=query.length();
    String w=query.substring(start,end); start=end+1;
    if(w.length()<2 || topicStopWord(w)) continue;
    total++;
    if(topicWordPresent(text,w)) hit++;
  }

  if(!total) return 0;
  return min(90,(hit*90)/total);
}

int entryScore(const String& query,const KnowledgeEntry& e){
  String q=norm(query), title=norm(e.title), aliases=String(e.aliases);
  if(q.indexOf(title)>=0 || title.indexOf(q)>=0) return 100;
  int best=tokenScore(q,title);
  int start=0;
  while(start<(int)aliases.length()){
    int end=aliases.indexOf('|',start); if(end<0) end=aliases.length();
    String a=aliases.substring(start,end); a.trim();
    if(a.length()){
      String na=norm(a);
      if(q.indexOf(na)>=0 || na.indexOf(q)>=0) best=max(best,98);
      best=max(best,tokenScore(q,na));
    }
    start=end+1;
  }
  return best;
}

String learnedKey(const char* prefix,uint8_t i){ return String(prefix)+String(i); }
String pendingKey(uint8_t i){ return "p"+String(i); }

bool freshness(String q){
  q=norm(q);
  const char* terms[]={"today","latest","current","currently","now","this week","news","weather","forecast","temperature","price","score","live","recent","2026","2027"};
  for(size_t i=0;i<sizeof(terms)/sizeof(terms[0]);i++) if(q.indexOf(terms[i])>=0) return true;
  return false;
}

uint8_t countLearned(){
  if(!prefsReady) return 0; uint8_t c=0;
  for(uint8_t i=0;i<MAX_LEARNED;i++) if(prefs.getString(learnedKey("q",i).c_str(),"").length()) c++;
  return c;
}

uint8_t countPending(){
  if(!prefsReady) return 0; uint8_t c=0;
  for(uint8_t i=0;i<MAX_PENDING;i++) if(prefs.getString(pendingKey(i).c_str(),"").length()) c++;
  return c;
}

bool addPending(String q){
  if(!prefsReady) return false;
  q=norm(q); if(!q.length()) return false;
  for(uint8_t i=0;i<MAX_PENDING;i++) if(norm(prefs.getString(pendingKey(i).c_str(),""))==q) return true;
  for(uint8_t i=0;i<MAX_PENDING;i++){
    if(!prefs.getString(pendingKey(i).c_str(),"").length()){
      prefs.putString(pendingKey(i).c_str(),q.substring(0,min((int)q.length(),180)));
      return true;
    }
  }
  // Queue full: shift oldest out.
  for(uint8_t i=1;i<MAX_PENDING;i++) prefs.putString(pendingKey(i-1).c_str(),prefs.getString(pendingKey(i).c_str(),""));
  prefs.putString(pendingKey(MAX_PENDING-1).c_str(),q.substring(0,min((int)q.length(),180)));
  return true;
}

void removePendingIndex(uint8_t idx){
  if(!prefsReady || idx>=MAX_PENDING) return;
  for(uint8_t i=idx+1;i<MAX_PENDING;i++) prefs.putString(pendingKey(i-1).c_str(),prefs.getString(pendingKey(i).c_str(),""));
  prefs.remove(pendingKey(MAX_PENDING-1).c_str());
}

bool looksKnowledge(String q){
  q=norm(q);
  const char* starts[]={"what ","who ","where ","when ","why ","how ","which ","define ","explain ","tell me about ","tell me more about ","can you tell me ","describe "};
  for(size_t i=0;i<sizeof(starts)/sizeof(starts[0]);i++) if(q.startsWith(starts[i])) return true;
  return false;
}

String topicGroup(String t){
  t=norm(t);
  if(t.indexOf("neuromorphic")>=0 || t.indexOf("spiking")>=0) return "neuromorphic";
  if(t.indexOf("electric")>=0 || t.indexOf("voltage")>=0 || t.indexOf("current")>=0 || t.indexOf("resistance")>=0 || t.indexOf("ohm")>=0) return "electricity";
  if(t.indexOf("network")>=0 || t.indexOf("wifi")>=0 || t.indexOf("bluetooth")>=0 || t.indexOf("api")>=0) return "networking";
  if(t.indexOf("esp32")>=0 || t.indexOf("psram")>=0) return "esp32";
  if(t.indexOf("arduino")>=0) return "arduino";
  if(t.indexOf("artificial intelligence")>=0 || t=="ai" || t.indexOf("neural network")>=0) return "ai";
  if(t.indexOf("algebra")>=0) return "algebra";
  if(t.indexOf("geometry")>=0) return "geometry";
  if(t.indexOf("kira")>=0 || t.indexOf("elli")>=0) return "kira";
  return t;
}

} // namespace

void kiraOfflineBegin(){
  prefsReady=prefs.begin("kira_off_v2",false);
  if(prefsReady) learnedHead=prefs.getUChar("lhead",0)%MAX_LEARNED;
  internetWasAvailable=kiraNetworkMode()!=KIRA_NET_OFFLINE;
  Serial.print("[OFFLINE V2] Built-in exhibition knowledge topics: "); Serial.println(PACK_COUNT);
  Serial.print("[OFFLINE V2] Offline quiz questions: "); Serial.println(QUIZ_COUNT);
  Serial.print("[OFFLINE V2] Learned verified records: "); Serial.println(countLearned());
  Serial.print("[OFFLINE V2] Pending online questions: "); Serial.println(countPending());
}

void kiraOfflineTick(){
  bool online=kiraNetworkMode()!=KIRA_NET_OFFLINE;
  if(online && !internetWasAvailable && countPending()){
    Serial.print("[OFFLINE V2] Internet restored. Pending questions ready: "); Serial.println(countPending());
  }
  internetWasAvailable=online;
}

bool kiraOfflineFreshnessSensitive(const String& q){ return freshness(q); }
uint8_t kiraOfflineLearnedCount(){ return countLearned(); }
uint8_t kiraOfflinePendingCount(){ return countPending(); }

void kiraOfflineStoreVerified(const String& question,const String& answer,int confidence){
  if(!prefsReady || !question.length() || !answer.length() || freshness(question)) return;
  String nq=norm(question);
  for(uint8_t i=0;i<MAX_LEARNED;i++){
    if(norm(prefs.getString(learnedKey("q",i).c_str(),""))==nq){
      String clipped=answer.substring(0,min((int)answer.length(),700));
      uint8_t conf=(uint8_t)constrain(confidence,0,100);
      if(prefs.getString(learnedKey("a",i).c_str(),"")==clipped && prefs.getUChar(learnedKey("c",i).c_str(),0)==conf) return;
      prefs.putString(learnedKey("a",i).c_str(),clipped);
      prefs.putUChar(learnedKey("c",i).c_str(),conf);
      return;
    }
  }
  uint8_t i=learnedHead;
  prefs.putString(learnedKey("q",i).c_str(),question.substring(0,min((int)question.length(),160)));
  prefs.putString(learnedKey("a",i).c_str(),answer.substring(0,min((int)answer.length(),700)));
  prefs.putUChar(learnedKey("c",i).c_str(),(uint8_t)constrain(confidence,0,100));
  learnedHead=(uint8_t)((learnedHead+1)%MAX_LEARNED);
  prefs.putUChar("lhead",learnedHead);
}

bool kiraOfflineFindLesson(const String& query,String& title,String& answer,int& score){
  title=""; answer=""; score=0;
  String nq=norm(query);

  // IMPORTANT: learned-cache and built-in-pack candidates are scored
  // independently. Previously a learned candidate scoring only 60/100
  // survived into the final generic 58-point PACK threshold. That is how
  // "who invented zero" could incorrectly return an unrelated stored
  // "who invented microcomputer" answer.

  String learnedTitle="";
  String learnedAnswer="";
  int learnedScore=0;

  if(prefsReady){
    for(uint8_t i=0;i<MAX_LEARNED;i++){
      String q=prefs.getString(learnedKey("q",i).c_str(),"");
      String a=prefs.getString(learnedKey("a",i).c_str(),"");
      if(!q.length()||!a.length()) continue;
      int s=tokenScore(nq,q);
      if(s>learnedScore){
        learnedScore=s;
        learnedTitle=q;
        learnedAnswer=a;
      }
    }

    // Learned answers are persistent user/device knowledge, so require a
    // genuinely strong subject match before using them automatically.
    if(learnedScore>=78 && learnedAnswer.length()){
      title=learnedTitle;
      answer=learnedAnswer;
      score=learnedScore;
      return true;
    }
  }

  String packTitle="";
  String packAnswer="";
  int packScore=0;

  for(uint8_t i=0;i<PACK_COUNT;i++){
    int s=entryScore(nq,PACK[i]);
    if(s>packScore){
      packScore=s;
      packTitle=PACK[i].title;
      packAnswer=PACK[i].answer;
    }
  }

  if(packScore>=62 && packAnswer.length()){
    title=packTitle;
    answer=packAnswer;
    score=packScore;
    return true;
  }

  // Report the strongest rejected candidate for diagnostics only.
  if(learnedScore>=packScore){
    title=learnedTitle;
    score=learnedScore;
  } else {
    title=packTitle;
    score=packScore;
  }
  answer="";
  return false;
}

bool kiraOfflineGetQuiz(const String& topic,uint16_t sequence,KiraOfflineQuizQuestion& out){
  out=KiraOfflineQuizQuestion();
  String group=topicGroup(topic);
  uint8_t matches[QUIZ_COUNT]; uint8_t count=0;
  for(uint8_t i=0;i<QUIZ_COUNT;i++){
    String qt=topicGroup(QUIZ[i].topic);
    if(qt==group || tokenScore(group,qt)>=75) matches[count++]=i;
  }
  if(!count) return false;
  const QuizEntry& e=QUIZ[matches[sequence%count]];
  out.found=true; out.topic=e.topic; out.question=e.q; out.a=e.a; out.b=e.b; out.c=e.c; out.d=e.d; out.correct=e.correct; out.why=e.why;
  return true;
}

String kiraOfflineStatus(){
  return "Offline Brain V2: "+String(PACK_COUNT)+" built-in topics, "+String(countLearned())+" learned verified answers, "+String(countPending())+" pending online questions.";
}

bool kiraOfflineHandleCommand(String q){
  q=norm(q);
  if(q=="offline knowledge status" || q=="knowledge status" || q=="cache status" || q=="offline cache status"){
    elliSay(kiraOfflineStatus()); return true;
  }
  if(q=="show cached answers" || q=="show offline cache"){
    Serial.println(); Serial.println("========== OFFLINE LEARNED CACHE ==========");
    uint8_t shown=0;
    if(prefsReady) for(uint8_t i=0;i<MAX_LEARNED;i++){
      String qq=prefs.getString(learnedKey("q",i).c_str(),""); if(!qq.length()) continue;
      Serial.print(++shown); Serial.print(". "); Serial.print(qq); Serial.print(" | confidence "); Serial.println(prefs.getUChar(learnedKey("c",i).c_str(),0));
    }
    if(!shown) Serial.println("(empty)");
    Serial.println("===========================================");
    elliSay(shown?"I printed the learned offline cache in Serial Monitor.":"The learned offline cache is empty. Built-in exhibition knowledge is still available.");
    return true;
  }
  if(q=="show pending queries" || q=="pending queries" || q=="show pending questions"){
    Serial.println(); Serial.println("========== PENDING ONLINE QUESTIONS ==========");
    uint8_t shown=0;
    if(prefsReady) for(uint8_t i=0;i<MAX_PENDING;i++){
      String pq=prefs.getString(pendingKey(i).c_str(),""); if(!pq.length()) continue;
      Serial.print(++shown); Serial.print(". "); Serial.println(pq);
    }
    if(!shown) Serial.println("(empty)");
    Serial.println("==============================================");
    elliSay(shown?"Pending online questions are printed in Serial Monitor.":"There are no pending online questions.");
    return true;
  }
  if(q.startsWith("delete pending query ") || q.startsWith("delete pending question ")){
    int n=0; for(int i=0;i<(int)q.length();i++) if(isdigit((unsigned char)q[i])){n=q.substring(i).toInt();break;}
    if(n<1 || n>countPending()){ elliSay("That pending question number does not exist."); return true; }
    removePendingIndex((uint8_t)(n-1)); elliSay("Pending question deleted."); return true;
  }
  if(q=="clear pending queries" || q=="clear pending questions"){
    if(prefsReady) for(uint8_t i=0;i<MAX_PENDING;i++) prefs.remove(pendingKey(i).c_str());
    elliSay("Pending online questions cleared."); return true;
  }
  if(q=="retry pending queries" || q=="retry pending questions" || q=="retry pending query"){
    if(kiraNetworkMode()==KIRA_NET_OFFLINE){ elliSay("I am still offline, so the pending questions cannot be retried yet."); return true; }
    if(!countPending()){ elliSay("There are no pending questions to retry."); return true; }
    String pq=prefs.getString(pendingKey(0).c_str(),"");
    Serial.print("[OFFLINE V2] Retrying pending question: "); Serial.println(pq);
    bool handled=kiraV1HandleStructuredWeb(pq);
    if(handled){ removePendingIndex(0); Serial.print("[OFFLINE V2] Pending questions remaining: "); Serial.println(countPending()); }
    else elliSay("The online router still could not handle that pending question, so I kept it in the queue.");
    return true;
  }
  return false;
}

bool kiraOfflineHandleKnowledge(String q){
  q=normalizeInput(q);
  bool offline=kiraNetworkMode()==KIRA_NET_OFFLINE;
  bool preferLocal=kiraNetworkExhibitionMode();
  if(!offline && !preferLocal) return false;
  if(!looksKnowledge(q)) return false;

  if(freshness(q)){
    if(!offline) return false; // online exhibition mode should use live sources.
    addPending(q);
    elliSay("That question needs current information. I am offline, so I did not guess from old data. I saved it in the pending online queue.");
    return true;
  }

  String title,answer; int score=0;
  if(kiraOfflineFindLesson(q,title,answer,score)){
    Serial.print("[OFFLINE KNOWLEDGE] "); Serial.print(title); Serial.print(" | match "); Serial.print(score); Serial.println("/100");
    elliSay(answer+" [Offline knowledge]");
    return true;
  }

  if(score>0){
    Serial.print("[OFFLINE KNOWLEDGE] weak candidate rejected: ");
    Serial.print(title.length()?title:"(none)");
    Serial.print(" | match ");
    Serial.print(score);
    Serial.println("/100");
  }

  if(!offline) return false;
  addPending(q);
  elliSay("I don't have a reliable offline entry for that yet. I saved the question so it can be retried when internet is available.");
  return true;
}

bool kiraOfflineSelfTest(){
  String t,a; int s=0;
  bool neu=kiraOfflineFindLesson("explain neuromorphic computers",t,a,s) && a.length()>40;
  bool esp=kiraOfflineFindLesson("what is esp32 s3",t,a,s) && t.indexOf("ESP32")>=0;
  KiraOfflineQuizQuestion q; bool quiz=kiraOfflineGetQuiz("electricity",0,q) && q.correct.length()==1;
  return neu && esp && quiz && PACK_COUNT>=20 && QUIZ_COUNT>=10;
}
