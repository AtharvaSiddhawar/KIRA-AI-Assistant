#include "kira_vad.h"

#include "kira_next_config.h"
#include "kira_metrics.h"

#include <math.h>

namespace {

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t FRAME_SAMPLES = 320;   // 20 ms @ 16 kHz
constexpr uint32_t FRAME_MS = 20;

// Speech start remains deliberately conservative.
constexpr uint8_t START_CONFIRM_FRAMES = 4;   // ~80 ms
constexpr uint8_t VALID_SPEECH_FRAMES = 8;    // ~160 ms

// Group 1 V2.6:
// We DO NOT hard-latch the endpoint after only ~120 ms anymore.
// Instead:
//   1. no human evidence for ~260 ms -> end candidate
//   2. genuine moderate speech during the grace period cancels candidate
//   3. caller finalizes after AUTO_STT_END_SILENCE_MS from last human evidence
//
// This keeps natural pauses possible while preventing steady fan noise from
// refreshing the turn for the full 15-second emergency cap.
constexpr uint32_t END_CANDIDATE_AFTER_MS = 260;
constexpr uint8_t RESUME_CONFIRM_FRAMES = 2;  // ~40 ms

struct VadState {
  bool ready=false;
  bool utterance=false;
  bool speechStarted=false;
  bool speechActive=false;

  uint32_t noiseLevel=120;
  uint32_t noiseDiff=80;
  uint32_t lastLevel=0;

  uint32_t lastHumanMs=0;
  uint32_t utteranceStartMs=0;
  uint32_t candidateMs=0;

  uint32_t idleFrames=0;
  uint32_t speechFrames=0;

  uint8_t startRun=0;
  uint8_t resumeRun=0;
  uint8_t confidence=0;

  bool candidateLogged=false;
};

VadState s;
portMUX_TYPE vadMux = portMUX_INITIALIZER_UNLOCKED;


uint32_t meanAbs(
  const int16_t* p,
  size_t n
){
  if(!p || !n) return 0;

  uint64_t sum=0;

  for(size_t i=0;i<n;i++){
    int32_t v=p[i];

    if(v<0) v=-v;

    sum+=(uint32_t)v;
  }

  return (uint32_t)(sum/n);
}


uint32_t meanDifference(
  const int16_t* p,
  size_t n
){
  if(!p || n<2) return 0;

  uint64_t sum=0;

  for(size_t i=1;i<n;i++){
    int32_t d=
      (int32_t)p[i]-
      (int32_t)p[i-1];

    if(d<0) d=-d;

    sum+=(uint32_t)d;
  }

  return (uint32_t)(sum/(n-1));
}


uint16_t zeroCrossings(
  const int16_t* p,
  size_t n
){
  if(!p || n<2) return 0;

  uint16_t c=0;

  for(size_t i=1;i<n;i++){
    if(
      (p[i-1]<0 && p[i]>=0) ||
      (p[i-1]>=0 && p[i]<0)
    ){
      c++;
    }
  }

  return c;
}


uint32_t smooth(
  uint32_t oldValue,
  uint32_t newValue,
  uint8_t newWeightPct
){
  return
    (
      oldValue*
      (100-newWeightPct)+
      newValue*
      newWeightPct
    )/
    100;
}


void processFrame(
  const int16_t* p,
  size_t n
){
  if(!p || n<80) return;

  uint32_t level=
    meanAbs(
      p,
      n
    );

  uint32_t diff=
    meanDifference(
      p,
      n
    );

  uint16_t zc=
    zeroCrossings(
      p,
      n
    );


  uint32_t noiseLevel;
  uint32_t noiseDiff;
  uint32_t lastLevel;
  bool utterance;

  portENTER_CRITICAL(
    &vadMux
  );

  noiseLevel=s.noiseLevel;
  noiseDiff=s.noiseDiff;
  lastLevel=s.lastLevel;
  utterance=s.utterance;

  portEXIT_CRITICAL(
    &vadMux
  );


  // ----------------------------------------------------------
  // IDLE BACKGROUND LEARNING
  // ----------------------------------------------------------
  if(!utterance){
    uint8_t weight=
      s.idleFrames<50
        ? 12
        : 3;

    noiseLevel=
      smooth(
        noiseLevel,
        level,
        weight
      );

    noiseDiff=
      smooth(
        noiseDiff,
        diff,
        weight
      );

    portENTER_CRITICAL(
      &vadMux
    );

    s.noiseLevel=
      max(
        (uint32_t)25,
        noiseLevel
      );

    s.noiseDiff=
      max(
        (uint32_t)20,
        noiseDiff
      );

    s.lastLevel=
      level;

    s.idleFrames++;

    s.confidence=0;

    portEXIT_CRITICAL(
      &vadMux
    );

    return;
  }


  uint32_t energyRatio=
    (
      level*
      100UL
    )/
    (
      noiseLevel+
      1UL
    );

  uint32_t diffRatio=
    (
      diff*
      100UL
    )/
    (
      noiseDiff+
      1UL
    );

  uint32_t modulation=
    level>lastLevel
      ? level-lastLevel
      : lastLevel-level;

  uint32_t modulationGate=
    max(
      (uint32_t)35,
      noiseLevel/
      5UL
    );


  int score=0;

  if(energyRatio>=190) score+=45;
  else if(energyRatio>=155) score+=32;
  else if(energyRatio>=130) score+=18;
  else if(energyRatio>=118) score+=8;

  if(diffRatio>=175) score+=30;
  else if(diffRatio>=145) score+=22;
  else if(diffRatio>=125) score+=12;

  if(
    modulation>=
    modulationGate*
    2UL
  ){
    score+=16;
  }
  else if(
    modulation>=
    modulationGate
  ){
    score+=7;
  }

  if(
    zc>=6 &&
    zc<=150
  ){
    score+=8;
  }

  if(
    level>=
    noiseLevel+
    120UL
  ){
    score+=5;
  }

  if(
    energyRatio<112 &&
    diffRatio<116
  ){
    score=0;
  }

  if(score>100) score=100;


  // ----------------------------------------------------------
  // HUMAN SPEECH EVIDENCE
  // ----------------------------------------------------------
  //
  // "Strong" is used only to START a turn.
  //
  // "Continue" is intentionally stricter than the old V2.5 voiceEvidence:
  // a small fan fluctuation (roughly 115-125% of baseline) must not refresh
  // lastHumanMs for 15 seconds.
  //
  // Quiet real speech can still qualify through high sample-difference or
  // genuine short-term modulation.
  // ----------------------------------------------------------

  bool strongStart=
    score>=52 &&
    energyRatio>=120 &&
    (
      diffRatio>=132 ||
      modulation>=
        modulationGate*
        2UL
    );


  bool humanContinue=
    (
      diffRatio>=138 &&
      energyRatio>=112 &&
      score>=32
    ) ||
    (
      modulation>=
        modulationGate*
        2UL &&
      energyRatio>=116 &&
      score>=30
    ) ||
    (
      energyRatio>=165 &&
      diffRatio>=116 &&
      score>=38
    ) ||
    (
      score>=52 &&
      energyRatio>=120
    );


  // Candidate recovery can be a little more permissive than normal
  // continuation, but still requires distinctly speech-like variation.
  bool resumeEvidence=
    (
      diffRatio>=130 &&
      energyRatio>=110 &&
      score>=28
    ) ||
    (
      modulation>=
        modulationGate*
        2UL &&
      energyRatio>=112 &&
      score>=28
    ) ||
    (
      energyRatio>=150 &&
      diffRatio>=114 &&
      score>=34
    );


  uint32_t now=
    millis();

  bool printStart=false;
  bool printCandidate=false;
  bool printResume=false;

  uint8_t printConfidence=
    (uint8_t)score;

  uint32_t printNoise=
    noiseLevel;


  portENTER_CRITICAL(
    &vadMux
  );

  s.lastLevel=
    level;

  s.confidence=
    (uint8_t)score;


  if(!s.speechStarted){

    if(strongStart){
      if(
        s.startRun<255
      ){
        s.startRun++;
      }
    }
    else{
      s.startRun=0;

      // Slowly follow stationary room/fan changes while we wait for the
      // person to begin speaking.
      s.noiseLevel=
        smooth(
          s.noiseLevel,
          level,
          1
        );

      s.noiseDiff=
        smooth(
          s.noiseDiff,
          diff,
          1
        );
    }


    if(
      s.startRun>=
      START_CONFIRM_FRAMES
    ){
      s.speechStarted=true;
      s.speechActive=true;

      s.speechFrames=
        START_CONFIRM_FRAMES;

      s.lastHumanMs=
        now;

      s.candidateMs=0;
      s.resumeRun=0;
      s.candidateLogged=false;

      printStart=true;
    }
  }
  else{

    if(humanContinue){

      s.speechActive=true;

      s.lastHumanMs=
        now;

      s.candidateMs=0;
      s.resumeRun=0;
      s.candidateLogged=false;

      if(
        s.speechFrames<
        0xFFFFFFFFUL
      ){
        s.speechFrames++;
      }
    }
    else{

      // While no human evidence is present, slowly absorb stationary fan
      // drift into the baseline. This makes the endpoint more stable instead
      // of letting a fan become "speech continuation".
      s.noiseLevel=
        smooth(
          s.noiseLevel,
          level,
          1
        );

      s.noiseDiff=
        smooth(
          s.noiseDiff,
          diff,
          1
        );


      uint32_t quietMs=
        s.lastHumanMs
          ? now-s.lastHumanMs
          : 0;


      if(
        s.candidateMs==0 &&
        quietMs>=
          END_CANDIDATE_AFTER_MS
      ){
        s.candidateMs=
          now;

        s.speechActive=false;
        s.resumeRun=0;

        if(
          !s.candidateLogged
        ){
          s.candidateLogged=true;
          printCandidate=true;
        }
      }


      if(
        s.candidateMs!=0
      ){

        if(resumeEvidence){
          if(
            s.resumeRun<255
          ){
            s.resumeRun++;
          }

          if(
            s.resumeRun>=
            RESUME_CONFIRM_FRAMES
          ){
            s.speechActive=true;

            s.lastHumanMs=
              now;

            s.candidateMs=0;
            s.resumeRun=0;
            s.candidateLogged=false;

            printResume=true;

            if(
              s.speechFrames<
              0xFFFFFFFFUL
            ){
              s.speechFrames++;
            }
          }
        }
        else{
          s.resumeRun=0;
        }
      }
    }
  }


  portEXIT_CRITICAL(
    &vadMux
  );


  if(printStart){
    Serial.print(
      "[VAD V2.6] speech_start confidence="
    );

    Serial.print(
      printConfidence
    );

    Serial.print(
      " noise="
    );

    Serial.println(
      printNoise
    );
  }


  if(printCandidate){
    Serial.print(
      "[VAD V2.6] endpoint_candidate confidence="
    );

    Serial.println(
      printConfidence
    );
  }


  if(printResume){
    Serial.print(
      "[VAD V2.6] speech_resume confidence="
    );

    Serial.println(
      printConfidence
    );
  }
}

} // namespace


void kiraVadBegin(){
#if KIRA_VAD_V2_ENABLED
  portENTER_CRITICAL(
    &vadMux
  );

  s=
    VadState();

  s.ready=true;

  portEXIT_CRITICAL(
    &vadMux
  );

  Serial.println(
    "[VAD V2.6] READY | two-stage fan-safe endpoint | frame=20ms"
  );
#endif
}


void kiraVadStartUtterance(){
#if KIRA_VAD_V2_ENABLED
  portENTER_CRITICAL(
    &vadMux
  );

  s.utterance=true;
  s.speechStarted=false;
  s.speechActive=false;

  s.speechFrames=0;
  s.startRun=0;
  s.resumeRun=0;

  s.candidateLogged=false;
  s.candidateMs=0;

  s.confidence=0;

  s.utteranceStartMs=
    millis();

  s.lastHumanMs=0;

  portEXIT_CRITICAL(
    &vadMux
  );
#endif
}


void kiraVadCancelUtterance(){
#if KIRA_VAD_V2_ENABLED
  portENTER_CRITICAL(
    &vadMux
  );

  s.utterance=false;
  s.speechStarted=false;
  s.speechActive=false;

  s.speechFrames=0;
  s.startRun=0;
  s.resumeRun=0;

  s.candidateLogged=false;
  s.candidateMs=0;

  s.confidence=0;
  s.lastHumanMs=0;

  portEXIT_CRITICAL(
    &vadMux
  );
#endif
}


void kiraVadProcessBlock(
  const int16_t* samples,
  size_t count
){
#if KIRA_VAD_V2_ENABLED
  if(
    !samples ||
    !count
  ){
    return;
  }

  size_t offset=0;

  while(offset<count){
    size_t n=
      min(
        FRAME_SAMPLES,
        count-offset
      );

    if(n>=80){
      processFrame(
        samples+offset,
        n
      );
    }

    offset+=n;
  }
#else
  (void)samples;
  (void)count;
#endif
}


bool kiraVadSpeechSeen(){
#if KIRA_VAD_V2_ENABLED
  bool v;

  portENTER_CRITICAL(
    &vadMux
  );

  v=
    s.speechStarted &&
    s.speechFrames>=
      VALID_SPEECH_FRAMES;

  portEXIT_CRITICAL(
    &vadMux
  );

  return v;
#else
  return false;
#endif
}


bool kiraVadSpeechActive(){
#if KIRA_VAD_V2_ENABLED
  bool v;

  portENTER_CRITICAL(
    &vadMux
  );

  v=
    s.speechActive;

  portEXIT_CRITICAL(
    &vadMux
  );

  return v;
#else
  return false;
#endif
}


bool kiraVadSpeechEnded(
  uint32_t silenceMs
){
#if KIRA_VAD_V2_ENABLED
  bool started;
  uint32_t lastHuman;
  uint32_t frames;

  portENTER_CRITICAL(
    &vadMux
  );

  started=
    s.speechStarted;

  lastHuman=
    s.lastHumanMs;

  frames=
    s.speechFrames;

  portEXIT_CRITICAL(
    &vadMux
  );


  if(
    !started ||
    frames<
      VALID_SPEECH_FRAMES ||
    lastHuman==0
  ){
    return false;
  }


  // V2.6 final endpoint is based on the last frame with convincing human
  // evidence, NOT on weak energy/noise and NOT on an early hard latch.
  return
    millis()-lastHuman>=
    silenceMs;
#else
  (void)silenceMs;
  return false;
#endif
}


uint8_t kiraVadConfidence(){
  uint8_t v;

  portENTER_CRITICAL(
    &vadMux
  );

  v=
    s.confidence;

  portEXIT_CRITICAL(
    &vadMux
  );

  return v;
}


uint32_t kiraVadNoiseLevel(){
  uint32_t v;

  portENTER_CRITICAL(
    &vadMux
  );

  v=
    s.noiseLevel;

  portEXIT_CRITICAL(
    &vadMux
  );

  return v;
}


uint32_t kiraVadLastLevel(){
  uint32_t v;

  portENTER_CRITICAL(
    &vadMux
  );

  v=
    s.lastLevel;

  portEXIT_CRITICAL(
    &vadMux
  );

  return v;
}


uint32_t kiraVadUtteranceDurationMs(){
  uint32_t start;
  bool active;

  portENTER_CRITICAL(
    &vadMux
  );

  start=
    s.utteranceStartMs;

  active=
    s.utterance;

  portEXIT_CRITICAL(
    &vadMux
  );

  return
    active &&
    start
      ? millis()-start
      : 0;
}
