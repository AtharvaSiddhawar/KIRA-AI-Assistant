#include <Arduino.h>
#include "elli_study.h"
#include "elli_query_frame.h"
#include "kira_ai_web.h"
#include "kira_network_v2.h"
#include "kira_offline_brain.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

enum StudyStyle : uint8_t { STUDY_BALANCED=0, STUDY_SIMPLE, STUDY_BRIEF, STUDY_DETAILED };
enum QuizDifficulty : uint8_t { QUIZ_EASY=0, QUIZ_MEDIUM, QUIZ_HARD };

bool studyActive=false;
StudyStyle studyStyle=STUDY_BALANCED;
QuizDifficulty quizDifficulty=QUIZ_MEDIUM;
String studyTopic;

bool quizActive=false;
bool quizWaitingForAnswer=false;
String quizQuestion,quizOptionA,quizOptionB,quizOptionC,quizOptionD,quizCorrect,quizWhy;
uint16_t quizTotal=0,quizCorrectCount=0;
uint16_t quizTarget=0; // 0 = open-ended
uint16_t offlineQuizSequence=0;

String stripField(String s){
  s.trim();
  if(s.startsWith("\"")&&s.endsWith("\"")&&s.length()>=2) s=s.substring(1,s.length()-1);
  s.trim(); return s;
}

String tagged(const String& text,const String& tag){
  String needle=tag+"="; int start=text.indexOf(needle); if(start<0)return "";
  start+=needle.length(); int end=text.indexOf("|||",start); if(end<0)end=text.length();
  return stripField(text.substring(start,end));
}

const char* styleName(){
  switch(studyStyle){case STUDY_SIMPLE:return "simple";case STUDY_BRIEF:return "brief";case STUDY_DETAILED:return "detailed";default:return "balanced";}
}
const char* difficultyName(){
  switch(quizDifficulty){case QUIZ_EASY:return "easy";case QUIZ_HARD:return "hard";default:return "medium";}
}

String optionForLetter(const String& letter){
  if(letter=="a")return quizOptionA;if(letter=="b")return quizOptionB;if(letter=="c")return quizOptionC;if(letter=="d")return quizOptionD;return "";
}

String normalizeLetter(String q){
  q=normalizeInput(q);
  if(q.startsWith("answer "))q=q.substring(7); else if(q.startsWith("option "))q=q.substring(7);
  q.trim(); if(q=="a"||q=="b"||q=="c"||q=="d")return q;
  if(normalizeInput(quizOptionA)==q)return "a"; if(normalizeInput(quizOptionB)==q)return "b";
  if(normalizeInput(quizOptionC)==q)return "c"; if(normalizeInput(quizOptionD)==q)return "d"; return "";
}

void printQuizQuestion(){
  Serial.println(); Serial.println("========== STUDY QUIZ V2 ==========");
  if(quizTarget){Serial.print("Progress: ");Serial.print(quizTotal+1);Serial.print("/");Serial.println(quizTarget);}
  Serial.print("Topic: ");Serial.println(studyTopic);
  Serial.print("Difficulty: ");Serial.println(difficultyName());
  Serial.print("Q: ");Serial.println(quizQuestion);
  Serial.print("A) ");Serial.println(quizOptionA);Serial.print("B) ");Serial.println(quizOptionB);
  Serial.print("C) ");Serial.println(quizOptionC);Serial.print("D) ");Serial.println(quizOptionD);
  Serial.println("===================================");
  elliSay("Choose A, B, C, or D.");
}

bool parseQuizEnvelope(String answer){
  answer.trim(); quizQuestion=tagged(answer,"Q"); quizOptionA=tagged(answer,"A"); quizOptionB=tagged(answer,"B");
  quizOptionC=tagged(answer,"C"); quizOptionD=tagged(answer,"D"); quizCorrect=normalizeInput(tagged(answer,"CORRECT")); quizWhy=tagged(answer,"WHY");
  return quizQuestion.length()&&quizOptionA.length()&&quizOptionB.length()&&quizOptionC.length()&&quizOptionD.length()&&
    (quizCorrect=="a"||quizCorrect=="b"||quizCorrect=="c"||quizCorrect=="d");
}

bool loadOfflineQuiz(){
  KiraOfflineQuizQuestion q;
  if(!kiraOfflineGetQuiz(studyTopic,offlineQuizSequence++,q)) return false;
  quizQuestion=q.question;quizOptionA=q.a;quizOptionB=q.b;quizOptionC=q.c;quizOptionD=q.d;quizCorrect=normalizeInput(q.correct);quizWhy=q.why;
  Serial.println("[STUDY V2] Offline exhibition quiz fallback.");
  return true;
}

bool generateQuizQuestion(){
  if(!studyTopic.length()){elliSay("Tell me a study topic first, for example: quiz me on electricity.");return true;}
  bool online=kiraNetworkMode()!=KIRA_NET_OFFLINE;

  if(online){
    KiraV1::QueryFrame frame; KiraV1::clearQueryFrame(frame);
    String seed="Explain "+studyTopic; KiraV1::analyzeQuery(seed,frame);
    String request="Create exactly one accurate multiple-choice study question about '"+studyTopic+"'. ";
    request+="Difficulty: "+String(difficultyName())+". Avoid repeating the same obvious question when possible. ";
    request+="Inside KIRA_ANSWER return exactly: Q=question|||A=option A|||B=option B|||C=option C|||D=option D|||CORRECT=A|||WHY=short explanation. ";
    request+="CORRECT must be only A, B, C, or D and only one option should be clearly correct.";
    KiraV1::AiWebResult result;
    bool ok=KiraV1::askGroqWeb(frame,request,result,"",true,studyTopic);
    if(ok&&result.success&&parseQuizEnvelope(result.answer)){
      Serial.println("[STUDY V2] Online AI quiz question ready.");
      quizActive=true;quizWaitingForAnswer=true;printQuizQuestion();return true;
    }
    Serial.println("[STUDY V2] Online quiz generation failed -> trying offline bank.");
  }

  if(loadOfflineQuiz()){
    quizActive=true;quizWaitingForAnswer=true;printQuizQuestion();return true;
  }

  quizWaitingForAnswer=false;
  elliSay(online ? "I couldn't build a clean online quiz question, and I don't have an offline quiz bank for that topic yet." : "I am offline and don't have a local quiz bank for that topic yet. Try ESP32, AI, neuromorphic computing, electricity, networking, Arduino, algebra, geometry, or KIRA.");
  return true;
}

void finishQuizIfNeeded(){
  if(quizTarget && quizTotal>=quizTarget){
    quizActive=false;quizWaitingForAnswer=false;
    elliSay("Quiz complete. Your score is "+String(quizCorrectCount)+" out of "+String(quizTotal)+".");
  }
}

bool handleQuizAnswer(String q){
  if(!quizWaitingForAnswer)return false;
  String letter=normalizeLetter(q); if(!letter.length())return false;
  quizWaitingForAnswer=false;quizTotal++;
  if(letter==quizCorrect){
    quizCorrectCount++;
    String r="Correct."; if(quizWhy.length())r+=" "+quizWhy; elliSay(r);
  }else{
    String right=optionForLetter(quizCorrect);String r="Not quite. The correct answer is "+quizCorrect;
    if(right.length())r+=", "+right;r+=".";if(quizWhy.length())r+=" "+quizWhy;elliSay(r);
  }
  if(quizTarget&&quizTotal>=quizTarget){finishQuizIfNeeded();return true;}
  if(quizTarget){return generateQuizQuestion();}
  elliSay("Say 'next question' when you're ready."); return true;
}

String stripQuizCount(String topic,uint16_t& count){
  count=0; String n=normalizeInput(topic); int at=n.lastIndexOf(" with ");
  if(at<0)return topic;
  String tail=n.substring(at+6); int number=tail.toInt();
  if(number>=1&&number<=20&&(tail.indexOf("question")>=0||tail.indexOf("mcq")>=0)){
    count=(uint16_t)number; String out=topic.substring(0,at);out.trim();return out;
  }
  return topic;
}

bool teachTopic(String topic){
  topic.trim(); if(!topic.length()){elliSay("Tell me what topic you want me to teach.");return true;}
  String n=normalizeInput(topic);
  if(n.endsWith(" simply")){studyStyle=STUDY_SIMPLE;topic=topic.substring(0,topic.length()-7);topic.trim();}
  else if(n.endsWith(" in detail")){studyStyle=STUDY_DETAILED;topic=topic.substring(0,topic.length()-10);topic.trim();}
  else if(n.endsWith(" briefly")){studyStyle=STUDY_BRIEF;topic=topic.substring(0,topic.length()-8);topic.trim();}

  studyActive=true;studyTopic=topic;
  bool online=kiraNetworkMode()!=KIRA_NET_OFFLINE;
  if(!online || kiraNetworkExhibitionMode()){
    String title,answer;int score=0;
    if(kiraOfflineFindLesson(topic,title,answer,score)){
      Serial.print("[STUDY V2] Offline lesson: ");Serial.println(title);
      elliSay(answer+" You can ask me to simplify it, give an example, or quiz you. [Offline lesson]");
      return true;
    }
    if(!online){
      kiraOfflineHandleKnowledge("explain "+topic);
      return true;
    }
  }

  KiraV1::QueryFrame frame;KiraV1::clearQueryFrame(frame);KiraV1::analyzeQuery("Explain "+topic,frame);
  String request="Teach me '"+topic+"' as a clear lesson. ";
  if(studyStyle==STUDY_SIMPLE)request+="Use simple beginner-friendly language and one concrete example. ";
  else if(studyStyle==STUDY_BRIEF)request+="Keep it concise and focus on the key idea. ";
  else if(studyStyle==STUDY_DETAILED)request+="Explain step by step with important reasoning, examples and common misunderstandings. ";
  else request+="Give a balanced explanation with key concepts and one useful example. ";
  request+="End with one short check-understanding question, but do not require multiple choice unless asked.";
  KiraV1::AiWebResult result;
  bool ok=KiraV1::askGroqWeb(frame,request,result,"",false,topic);
  if(ok&&result.success&&result.answer.length()){elliSay(result.answer);return true;}

  String title,answer;int score=0;
  if(kiraOfflineFindLesson(topic,title,answer,score)){elliSay(answer+" [Offline fallback]");return true;}
  elliSay("I couldn't reach an online teaching provider and I don't have that topic in my offline lesson pack yet.");
  return true;
}

} // namespace

void elliStudyBegin(){
  studyActive=false;studyStyle=STUDY_BALANCED;quizDifficulty=QUIZ_MEDIUM;studyTopic="";
  quizActive=false;quizWaitingForAnswer=false;quizTotal=0;quizCorrectCount=0;quizTarget=0;offlineQuizSequence=0;
  Serial.println("[STUDY] Study Mode V2 ready: online teaching + online/offline quiz fallback.");
}

bool elliStudyModeActive(){return studyActive;}

String elliStudyStatus(){
  String out="Study mode is ";out+=studyActive?"on":"off";out+=". Style: ";out+=styleName();out+=". Quiz difficulty: ";out+=difficultyName();out+=".";
  if(studyTopic.length())out+=" Topic: "+studyTopic+".";
  if(quizTotal)out+=" Quiz score: "+String(quizCorrectCount)+"/"+String(quizTotal)+".";
  return out;
}

String elliStudyDecorateKnowledgeQuery(const String& q){
  if(!studyActive)return q;
  String out=q+" Study mode instruction: teach the answer clearly, check the key idea, and avoid unnecessary filler.";
  switch(studyStyle){
    case STUDY_SIMPLE:out+=" Use beginner-friendly language, define unavoidable technical terms, and include one simple example when useful.";break;
    case STUDY_BRIEF:out+=" Keep it short: give the key idea first and only the most important supporting detail.";break;
    case STUDY_DETAILED:out+=" Give a structured, step-by-step explanation with important reasoning, examples, and common misunderstandings when relevant.";break;
    default:out+=" Give a balanced explanation with the main concept and one useful example when relevant.";break;
  }
  if(studyTopic.length())out+=" Current study topic: '"+studyTopic+"'. Use it only when relevant to the user's question.";
  return out;
}

bool elliStudyHandleCommand(String q){
  q=normalizeInput(q);
  if(handleQuizAnswer(q))return true;

  if(q.startsWith("teach me "))return teachTopic(q.substring(9));
  if(q.startsWith("teach "))return teachTopic(q.substring(6));

  if(q=="study mode on"||q=="start study mode"||q=="study mode"){
    studyActive=true;elliSay("Study mode is on. Say 'teach me' followed by a topic, ask a question, or say 'quiz me on' followed by a topic.");return true;
  }
  if(q=="study mode off"||q=="stop study mode"||q=="exit study mode"){
    studyActive=false;quizActive=false;quizWaitingForAnswer=false;elliSay("Study mode is off.");return true;
  }
  if(q=="study status"||q=="study mode status"){elliSay(elliStudyStatus());return true;}

  if(q=="study style simple"){studyStyle=STUDY_SIMPLE;studyActive=true;elliSay("Study style set to simple.");return true;}
  if(q=="study style brief"){studyStyle=STUDY_BRIEF;studyActive=true;elliSay("Study style set to brief.");return true;}
  if(q=="study style detailed"||q=="study style detail"){studyStyle=STUDY_DETAILED;studyActive=true;elliSay("Study style set to detailed.");return true;}
  if(q=="study style balanced"){studyStyle=STUDY_BALANCED;studyActive=true;elliSay("Study style set to balanced.");return true;}

  if(q=="quiz difficulty easy"||q=="make quiz easier"||q=="make it easier"){quizDifficulty=QUIZ_EASY;elliSay("Quiz difficulty set to easy.");return true;}
  if(q=="quiz difficulty medium"){quizDifficulty=QUIZ_MEDIUM;elliSay("Quiz difficulty set to medium.");return true;}
  if(q=="quiz difficulty hard"||q=="make quiz harder"||q=="make it harder"){quizDifficulty=QUIZ_HARD;elliSay("Quiz difficulty set to hard.");return true;}

  if(q=="quiz score"||q=="show quiz score"||q=="my quiz score"){
    elliSay("Your quiz score is "+String(quizCorrectCount)+" out of "+String(quizTotal)+".");return true;
  }
  if(q=="stop quiz"||q=="end quiz"||q=="cancel quiz"){
    quizActive=false;quizWaitingForAnswer=false;elliSay("Quiz stopped. Your current score is "+String(quizCorrectCount)+" out of "+String(quizTotal)+".");return true;
  }
  if(q=="next question"||q=="another question"||q=="next quiz question"){
    if(!quizActive&&!studyTopic.length()){elliSay("Tell me what topic you want to be quizzed on first.");return true;}
    return generateQuizQuestion();
  }

  String topic;
  if(q.startsWith("quiz me on "))topic=q.substring(11);
  else if(q.startsWith("quiz me about "))topic=q.substring(14);
  else if(q=="quiz me"){
    if(!studyTopic.length()){elliSay("What topic should I quiz you on?");return true;}topic=studyTopic;
  }
  topic.trim();
  if(topic.length()){
    uint16_t target=0;topic=stripQuizCount(topic,target);studyActive=true;studyTopic=topic;quizActive=true;quizWaitingForAnswer=false;
    quizTotal=0;quizCorrectCount=0;quizTarget=target;offlineQuizSequence=0;
    return generateQuizQuestion();
  }

  if(q.startsWith("study topic "))topic=q.substring(12);
  else if(q.startsWith("study ")&&!q.startsWith("study mode ")&&!q.startsWith("study style "))topic=q.substring(6);
  topic.trim();
  if(topic.length()){studyActive=true;studyTopic=topic;elliSay("Study topic set to "+studyTopic+". Ask me to teach it, explain something, compare ideas, or say 'quiz me'.");return true;}
  return false;
}
