#include <Arduino.h>
#include "elli_context.h"

namespace KiraV1 {
static const unsigned long CONTEXT_TIMEOUT_MS=90000UL;
static String norm(String s){s.toLowerCase();String o="";bool sp=false;for(size_t i=0;i<s.length();i++){char c=s[i];if(isalnum((unsigned char)c)){o+=c;sp=false;}else if(o.length()&&!sp){o+=' ';sp=true;}}o.trim();while(o.indexOf("  ")>=0)o.replace("  "," ");return o;}
static int tokenize(const String& in,String out[],int maxc){String s=norm(in);int c=0,p=0;while(p<s.length()&&c<maxc){while(p<s.length()&&s[p]==' ')p++;if(p>=s.length())break;int e=s.indexOf(' ',p);if(e<0)e=s.length();out[c++]=s.substring(p,e);p=e+1;}return c;}
static bool stop(const String& w){const char* const a[]={"the","a","an","one","that","please","i","mean","meant","it","this","first","second","1","2"};for(size_t i=0;i<sizeof(a)/sizeof(a[0]);i++)if(w==a[i])return true;return false;}
static int overlap(const String& r,const String& c){String a[16],b[48];int an=tokenize(r,a,16),bn=tokenize(c,b,48),use=0,hit=0;for(int i=0;i<an;i++){if(stop(a[i]))continue;use++;for(int j=0;j<bn;j++){if(a[i]==b[j]){hit++;break;}}}return use?(hit*100)/use:0;}
ConversationContext::ConversationContext(){clear();}
void ConversationContext::clear(){pending_=PendingContext();clearQueryFrame(pending_.originalFrame);}
bool ConversationContext::active()const{return pending_.active;}

bool ConversationContext::live(){
  if(!pending_.active) return false;
  if(expired()){
    clear();
    return false;
  }
  return true;
}

ContextKind ConversationContext::kind()const{return pending_.kind;}
bool ConversationContext::expired()const{return pending_.active && (millis()-pending_.createdAt)>CONTEXT_TIMEOUT_MS;}
bool ConversationContext::isCancelReply(const String& reply){String q=norm(reply);const char* const p[]={"cancel","never mind","nevermind","forget it","leave it","something else","stop","skip","skip it","neither","none","not sure","i am not sure","im not sure","i don t know","dont know","i dont know","no idea"};for(size_t i=0;i<sizeof(p)/sizeof(p[0]);i++)if(q==p[i])return true;return false;}
void ConversationContext::beginEntityChoice(const QueryFrame& f,const Candidate& a,const Candidate& b){clear();pending_.active=true;pending_.kind=CONTEXT_ENTITY_CHOICE;pending_.originalFrame=f;pending_.first=a;pending_.second=b;pending_.createdAt=millis();pending_.prompt="I found two close matches: 1) "+candidateLabel(a)+"  2) "+candidateLabel(b)+". Which one do you mean?";}
void ConversationContext::beginMissingLocation(const QueryFrame& f){clear();pending_.active=true;pending_.kind=CONTEXT_MISSING_LOCATION;pending_.originalFrame=f;pending_.createdAt=millis();pending_.prompt="Which place do you mean?";}
void ConversationContext::beginMissingRegion(const QueryFrame& f){clear();pending_.active=true;pending_.kind=CONTEXT_MISSING_REGION;pending_.originalFrame=f;pending_.createdAt=millis();pending_.prompt="Which country or region do you mean?";}
void ConversationContext::beginMissingCriterion(const QueryFrame& f){clear();pending_.active=true;pending_.kind=CONTEXT_MISSING_CRITERION;pending_.originalFrame=f;pending_.createdAt=millis();pending_.prompt="What criterion or ranking should I use?";}
String ConversationContext::prompt()const{return pending_.prompt;}
String ConversationContext::cleanFollowUp(const String& reply){String q=norm(reply);const char* const p[]={"i mean ","i meant ","the ","that ","its ","it is "};bool changed=true;while(changed){changed=false;for(size_t i=0;i<sizeof(p)/sizeof(p[0]);i++){String x=p[i];if(q.startsWith(x)){q.remove(0,x.length());q.trim();changed=true;break;}}}return q;}
static String normalizeCriterionValue(String q){
  q=norm(q);
  if(q=="all" || q=="every" || q=="everything" || q=="overall" || q=="all criteria" || q=="all factors" || q=="all of them") return "overall";
  if(q=="thickeness" || q=="thikness" || q=="thicknesss") return "thickness";
  return q;
}
int ConversationContext::matchCandidateReply(const String& reply,const Candidate& c){String q=cleanFollowUp(reply);String text=c.title+" "+c.description+" "+c.aliases+" "+entityTypeName(c.type);String nt=norm(text);int s=0;if(norm(c.title)==q)s+=100;String type=norm(entityTypeName(c.type));if(q==type&&type!="entity")s+=85;if(q.length()>=3&&nt.indexOf(q)>=0)s+=60;s+=(overlap(q,text)*45)/100;return min(100,s);}
ContextResolution ConversationContext::consume(const String& reply){
  ContextResolution out;clearQueryFrame(out.frame);
  if(!pending_.active)return out;
  if(expired()){clear();out.result=CONTEXT_EXPIRED;out.message="That clarification expired, so I'll treat your next message as a new request.";return out;}
  if(isCancelReply(reply)){clear();out.result=CONTEXT_CANCELLED;out.message="Okay, I cancelled that clarification.";return out;}
  pending_.attempts++;
  if(pending_.kind==CONTEXT_ENTITY_CHOICE){
    String q=norm(reply);int selected=-1;
    if(q=="1"||q=="one"||q=="first"||q=="first one"||q=="option 1"||q=="option one"||q=="number 1"||q=="number one")selected=0;
    else if(q=="2"||q=="two"||q=="second"||q=="second one"||q=="option 2"||q=="option two"||q=="number 2"||q=="number two")selected=1;
    if(selected<0){int a=matchCandidateReply(reply,pending_.first),b=matchCandidateReply(reply,pending_.second);if(a>=55&&a-b>=15)selected=0;else if(b>=55&&b-a>=15)selected=1;}
    if(selected>=0){out.result=CONTEXT_RESOLVED;out.frame=pending_.originalFrame;out.chosen=selected==0?pending_.first:pending_.second;out.hasChosenCandidate=true;out.message="Got it — "+out.chosen.title+".";clear();return out;}
    if(pending_.attempts>=3){out.result=CONTEXT_CANCELLED;out.message="I still couldn't identify which match you meant, so I cleared that clarification. Ask the question again with one extra detail when you're ready.";clear();return out;}
    out.result=CONTEXT_NEEDS_MORE;out.message="I still have two close matches. Say 1 or 2, one or two, or describe which one you mean: "+candidateLabel(pending_.first)+" / "+candidateLabel(pending_.second)+".";return out;
  }
  String value=cleanFollowUp(reply);
  if(!value.length()){out.result=CONTEXT_NEEDS_MORE;out.message=pending_.prompt;return out;}
  out.frame=pending_.originalFrame;
  if(pending_.kind==CONTEXT_MISSING_LOCATION){out.frame.location=value;out.frame.subject=value;out.frame.ambiguous=false;}
  else if(pending_.kind==CONTEXT_MISSING_REGION){out.frame.region=value;out.frame.ambiguous=false;}
  else if(pending_.kind==CONTEXT_MISSING_CRITERION){out.frame.criterion=normalizeCriterionValue(value);out.frame.ambiguous=false;}
  out.result=CONTEXT_RESOLVED;out.message="Got it.";clear();return out;
}
} // namespace KiraV1
