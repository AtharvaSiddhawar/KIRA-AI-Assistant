#include <Arduino.h>
#include "elli_entity_resolver.h"

namespace KiraV1 {

static String norm(String s){
  s.toLowerCase(); String o=""; bool sp=false;
  for(size_t i=0;i<s.length();i++){
    char c=s[i];
    if(isalnum((unsigned char)c)){o+=c;sp=false;}
    else if(o.length()&&!sp){o+=' ';sp=true;}
  }
  o.trim(); while(o.indexOf("  ")>=0)o.replace("  "," "); return o;
}
static String compact(String s){s=norm(s);s.replace(" ","");return s;}
static int tokenize(const String& in,String out[],int maxc){String s=norm(in);int c=0,p=0;while(p<s.length()&&c<maxc){while(p<s.length()&&s[p]==' ')p++;if(p>=s.length())break;int e=s.indexOf(' ',p);if(e<0)e=s.length();out[c++]=s.substring(p,e);p=e+1;}return c;}
static bool stop(const String& w){const char* const a[]={"a","an","the","is","are","was","were","what","which","who","where","when","why","how","of","in","at","on","for","from","to","by","with","about","located","location","please"};for(size_t i=0;i<sizeof(a)/sizeof(a[0]);i++)if(w==a[i])return true;return false;}
static int coverage(const String& wanted,const String& cand){String a[20],b[48];int an=tokenize(wanted,a,20),bn=tokenize(cand,b,48),use=0,hit=0;for(int i=0;i<an;i++){if(stop(a[i]))continue;use++;for(int j=0;j<bn;j++){if(a[i]==b[j]){hit++;break;}}}return use?(hit*100)/use:0;}
static int extraPenalty(const String& wanted,const String& title){String a[20],b[20];int an=tokenize(wanted,a,20),bn=tokenize(title,b,20),ua=0,ub=0;for(int i=0;i<an;i++)if(!stop(a[i]))ua++;for(int i=0;i<bn;i++)if(!stop(b[i]))ub++;return min(24,max(0,ub-ua)*6);}

EntityType entityTypeFromText(const String& value){
  String t=norm(value);
  if(t=="place")return ENTITY_PLACE; if(t=="country")return ENTITY_COUNTRY; if(t=="city")return ENTITY_CITY;
  if(t=="monument"||t=="mausoleum"||t=="landmark")return ENTITY_MONUMENT;
  if(t=="hotel"||t=="resort")return ENTITY_HOTEL; if(t=="person")return ENTITY_PERSON;
  if(t=="organization"||t=="organisation"||t=="company"||t=="university")return ENTITY_ORGANIZATION;
  if(t=="list"||t=="list page")return ENTITY_LIST_PAGE; if(t=="disambiguation")return ENTITY_DISAMBIGUATION;
  return ENTITY_UNKNOWN;
}

EntityType inferEntityType(const Candidate& c){
  if(c.type!=ENTITY_UNKNOWN) return c.type;
  String t=norm(c.title+" "+c.description);
  if(t.startsWith("list of ")||c.title.startsWith("List of "))return ENTITY_LIST_PAGE;
  if(t.indexOf("disambiguation")>=0)return ENTITY_DISAMBIGUATION;
  if(t.indexOf("hotel")>=0||t.indexOf("resort")>=0)return ENTITY_HOTEL;
  if(t.indexOf("monument")>=0||t.indexOf("mausoleum")>=0||t.indexOf("heritage site")>=0||t.indexOf("landmark")>=0)return ENTITY_MONUMENT;
  if(t.indexOf("country")>=0||t.indexOf("sovereign state")>=0||t.indexOf("nation in ")>=0)return ENTITY_COUNTRY;
  if(t.indexOf("city")>=0||t.indexOf("municipality")>=0||t.indexOf("town")>=0)return ENTITY_CITY;
  if(t.indexOf("company")>=0||t.indexOf("organization")>=0||t.indexOf("organisation")>=0||t.indexOf("university")>=0)return ENTITY_ORGANIZATION;
  if(t.indexOf("born ")>=0||t.indexOf("scientist")>=0||t.indexOf("actor")>=0||t.indexOf("politician")>=0)return ENTITY_PERSON;
  if(t.indexOf("located in")>=0||t.indexOf("situated in")>=0||t.indexOf("place in")>=0)return ENTITY_PLACE;
  return ENTITY_UNKNOWN;
}

const char* entityTypeName(EntityType t){switch(t){case ENTITY_PLACE:return "place";case ENTITY_COUNTRY:return "country";case ENTITY_CITY:return "city";case ENTITY_MONUMENT:return "monument";case ENTITY_HOTEL:return "hotel";case ENTITY_PERSON:return "person";case ENTITY_ORGANIZATION:return "organization";case ENTITY_LIST_PAGE:return "list page";case ENTITY_DISAMBIGUATION:return "disambiguation";default:return "entity";}}

int scoreCandidate(const QueryFrame& f,Candidate& c){
  c.type=inferEntityType(c);
  String wanted=f.entity.length()?f.entity:f.subject;
  String wn=norm(wanted),tn=norm(c.title);
  int score=0;
  if(wn.length()&&tn==wn)score+=58;
  else if(compact(wanted).length()&&compact(wanted)==compact(c.title))score+=54;
  else score+=(coverage(wanted,c.title)*36)/100;
  score+=(coverage(wanted,c.description+" "+c.aliases)*18)/100;
  String d=norm(c.description);
  if(f.intent==INTENT_ENTITY_LOCATION && (d.indexOf("located")>=0||d.indexOf("situated")>=0||d.indexOf("city")>=0||d.indexOf("country")>=0||d.indexOf("state")>=0||d.indexOf("district")>=0))score+=10;
  if(c.type==ENTITY_LIST_PAGE)score-=45;
  if(c.type==ENTITY_DISAMBIGUATION)score-=35;
  score-=extraPenalty(wanted,c.title);
  score+=min(8,max(0,c.retrievalScore/12));
  score=constrain(score,0,100); c.semanticScore=score;c.finalScore=score;return score;
}

ResolutionDecision resolveCandidates(const QueryFrame& f,Candidate c[],int count){
  ResolutionDecision r;
  if(count<=0){r.action=RESOLVE_ASK_REPHRASE;return r;}
  for(int i=0;i<count;i++)scoreCandidate(f,c[i]);
  int first=-1,second=-1;
  for(int i=0;i<count;i++){
    if(first<0||c[i].finalScore>c[first].finalScore){second=first;first=i;}
    else if(second<0||c[i].finalScore>c[second].finalScore)second=i;
  }
  r.firstIndex=first;r.firstScore=c[first].finalScore;
  if(second>=0){r.secondIndex=second;r.secondScore=c[second].finalScore;}
  r.gap=r.firstScore-r.secondScore;
  if(r.firstScore>=90&&(second<0||r.gap>=18)){r.action=RESOLVE_ACCEPT_TOP;return r;}
  if(second>=0&&r.firstScore>=70&&r.secondScore>=60&&r.gap<18){r.action=RESOLVE_ASK_TOP_TWO;return r;}
  if(r.firstScore>=78&&(second<0||r.secondScore<55)){r.action=RESOLVE_ACCEPT_TOP;return r;}
  r.action=RESOLVE_ASK_REPHRASE;return r;
}

String candidateLabel(const Candidate& c){String o=c.title;String t=entityTypeName(c.type);if(t.length()){o+=" — ";o+=t;}if(c.description.length()){String d=c.description;if(d.length()>105)d=d.substring(0,105)+"...";o+=": ";o+=d;}return o;}

} // namespace KiraV1
