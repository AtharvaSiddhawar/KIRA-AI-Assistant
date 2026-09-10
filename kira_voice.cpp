#include <Arduino.h>
#include <WiFi.h>
#include <ESP_I2S.h>
#include <ESP_SR.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "kira_voice.h"
#include "kira_sr_bridge.h"
#include "kira_audio_engine.h"
#include "kira_runtime.h"
#include "kira_wake.h"
#include "kira_vad.h"
#include "kira_metrics.h"
#include "kira_next_config.h"
#include "kira_tts.h"
#include "elli_visual.h"
#include "kira_network_v2.h"
#include "kira_wifi_max.h"
#include "kira_secrets.h"

#ifndef KIRA_GROQ_API_KEY
#define KIRA_GROQ_API_KEY ""
#endif

// Latest reply produced by the Universal KIRA brain.
extern String lastElliUtterance;


namespace {


// ============================================================
// HARDWARE
// ============================================================

// INMP441
constexpr int MIC_BCLK = 38;
constexpr int MIC_WS   = 39;
constexpr int MIC_DIN  = 40;

// MAX98357A
constexpr int AMP_BCLK = 15;
constexpr int AMP_LRC  = 16;
constexpr int AMP_DIN  = 17;
constexpr int AMP_SD   = 18;

constexpr uint32_t MIC_SAMPLE_RATE = 16000;
constexpr uint32_t SPK_SAMPLE_RATE = 22050;
constexpr int16_t SPEAKER_VOLUME   = 3000;

I2SClass MicI2S(I2S_NUM_0);
I2SClass SpeakerI2S(I2S_NUM_1);


// ============================================================
// OFFLINE MULTINET COMMAND IDS
// ============================================================
//
// IMPORTANT:
// These are speech COMMANDS after WakeNet wakes Elli.
// They are not additional WakeNet wake words.
// ============================================================

enum KiraVoiceCommandId {

  VOICE_HELLO = 0,
  VOICE_LIGHT_ON,
  VOICE_LIGHT_OFF,
  VOICE_FAN_ON,
  VOICE_FAN_OFF,
  VOICE_CHARGER_ON,
  VOICE_CHARGER_OFF,
  VOICE_SECOND_LIGHT_ON,
  VOICE_SECOND_LIGHT_OFF,
  VOICE_ALL_ON,
  VOICE_ALL_OFF,
  VOICE_SCENE_STUDY,
  VOICE_SCENE_LEAVING,
  VOICE_SCENE_NIGHT,
  VOICE_SCENE_MORNING,
  VOICE_SCENE_SLEEP,
  VOICE_WIFI_RESCAN,
  VOICE_NETWORK_STATUS,
  VOICE_OFFLINE_ON,
  VOICE_OFFLINE_OFF,
  VOICE_EXHIBITION_ON,
  VOICE_EXHIBITION_OFF,
  VOICE_TIME,
  VOICE_DATE,
  VOICE_SYSTEM_STATUS,
  VOICE_MEMORY_STATUS,
  VOICE_BRAIN_STATUS,
  VOICE_STOPWATCH_START,
  VOICE_STOPWATCH_STOP,
  VOICE_STOPWATCH_RESET,
  VOICE_STOPWATCH_STATUS,
  VOICE_TIMER_10S,
  VOICE_TIMER_30S,
  VOICE_TIMER_1M,
  VOICE_TIMER_2M,
  VOICE_TIMER_5M,
  VOICE_TIMER_10M,
  VOICE_TIMER_15M,
  VOICE_TIMER_20M,
  VOICE_TIMER_30M,
  VOICE_TIMER_1H,
  VOICE_TIMER_PAUSE,
  VOICE_TIMER_RESUME,
  VOICE_TIMER_CANCEL,
  VOICE_TIMER_STATUS,
  VOICE_ALARM_LIST,
  VOICE_ALARM_STOP,
  VOICE_ALARM_CANCEL_ALL,
  VOICE_NOTES_LIST,
  VOICE_TASKS_LIST,
  VOICE_TASKS_COMPLETED,
  VOICE_ROUTINES_LIST,
  VOICE_MEMORY_RECALL,
  VOICE_OFFLINE_KNOWLEDGE_STATUS,
  VOICE_PENDING_QUERIES,
  VOICE_QUIZ_SCORE,

  // Phase 5J: two-stage free-form online STT gate.
  // Say the stock wake word, then one of the "Ask Elli" aliases,
  // wait for the short listening beep, and speak a normal sentence.
  VOICE_FREEFORM_STT,

  VOICE_STOP
};

// Internal queue-only event. This is never registered as a MultiNet phrase.
// It means MultiNet timed out after the wake word and the bridge preserved
// the already-spoken command audio for automatic STT fallback.
constexpr int VOICE_AUTO_STT_FALLBACK = 0x5A20;


// ============================================================
// MULTINET COMMAND PHRASES
// ============================================================
//
// ESP-SR supports many aliases per command ID.
// Keep phrases simple and natural for better offline accuracy.
// ============================================================

static const sr_cmd_t voiceCommands[] = {

  // KIRA Phase 5I-A expanded offline vocabulary.
  // Keep spoken phrases word-based: MultiNet command phrases
  // should not contain Arabic numerals or special characters.

  // VOICE_HELLO
  { VOICE_HELLO, "Hello Elli" },
  { VOICE_HELLO, "Hi Elli" },
  { VOICE_HELLO, "Hey Elli" },
  { VOICE_HELLO, "Okay Elli" },

  // VOICE_LIGHT_ON
  { VOICE_LIGHT_ON, "Turn on the light" },
  { VOICE_LIGHT_ON, "Switch on the light" },
  { VOICE_LIGHT_ON, "Light on" },
  { VOICE_LIGHT_ON, "Lights on" },
  { VOICE_LIGHT_ON, "Main light on" },

  // VOICE_LIGHT_OFF
  { VOICE_LIGHT_OFF, "Turn off the light" },
  { VOICE_LIGHT_OFF, "Switch off the light" },
  { VOICE_LIGHT_OFF, "Light off" },
  { VOICE_LIGHT_OFF, "Lights off" },
  { VOICE_LIGHT_OFF, "Main light off" },

  // VOICE_FAN_ON
  { VOICE_FAN_ON, "Turn on the fan" },
  { VOICE_FAN_ON, "Switch on the fan" },
  { VOICE_FAN_ON, "Fan on" },
  { VOICE_FAN_ON, "Start fan" },
  { VOICE_FAN_ON, "Start the fan" },

  // VOICE_FAN_OFF
  { VOICE_FAN_OFF, "Turn off the fan" },
  { VOICE_FAN_OFF, "Switch off the fan" },
  { VOICE_FAN_OFF, "Fan off" },
  { VOICE_FAN_OFF, "Stop fan" },
  { VOICE_FAN_OFF, "Stop the fan" },

  // VOICE_CHARGER_ON
  { VOICE_CHARGER_ON, "Turn on the charger" },
  { VOICE_CHARGER_ON, "Charger on" },
  { VOICE_CHARGER_ON, "Start charging" },

  // VOICE_CHARGER_OFF
  { VOICE_CHARGER_OFF, "Turn off the charger" },
  { VOICE_CHARGER_OFF, "Charger off" },
  { VOICE_CHARGER_OFF, "Stop charging" },

  // VOICE_SECOND_LIGHT_ON
  { VOICE_SECOND_LIGHT_ON, "Turn on second light" },
  { VOICE_SECOND_LIGHT_ON, "Second light on" },
  { VOICE_SECOND_LIGHT_ON, "Light two on" },

  // VOICE_SECOND_LIGHT_OFF
  { VOICE_SECOND_LIGHT_OFF, "Turn off second light" },
  { VOICE_SECOND_LIGHT_OFF, "Second light off" },
  { VOICE_SECOND_LIGHT_OFF, "Light two off" },

  // VOICE_ALL_ON
  { VOICE_ALL_ON, "Turn everything on" },
  { VOICE_ALL_ON, "All devices on" },
  { VOICE_ALL_ON, "All on" },
  { VOICE_ALL_ON, "Switch everything on" },

  // VOICE_ALL_OFF
  { VOICE_ALL_OFF, "Turn everything off" },
  { VOICE_ALL_OFF, "All devices off" },
  { VOICE_ALL_OFF, "All off" },
  { VOICE_ALL_OFF, "Switch everything off" },

  // VOICE_SCENE_STUDY
  { VOICE_SCENE_STUDY, "Study mode" },
  { VOICE_SCENE_STUDY, "Study scene" },
  { VOICE_SCENE_STUDY, "Start study" },
  { VOICE_SCENE_STUDY, "Lets study" },
  { VOICE_SCENE_STUDY, "Focus mode" },
  { VOICE_SCENE_STUDY, "Focus scene" },

  // VOICE_SCENE_LEAVING
  { VOICE_SCENE_LEAVING, "Leaving mode" },
  { VOICE_SCENE_LEAVING, "Leaving scene" },
  { VOICE_SCENE_LEAVING, "I am leaving" },
  { VOICE_SCENE_LEAVING, "I am going out" },
  { VOICE_SCENE_LEAVING, "Going out" },
  { VOICE_SCENE_LEAVING, "Away mode" },

  // VOICE_SCENE_NIGHT
  { VOICE_SCENE_NIGHT, "Good night" },
  { VOICE_SCENE_NIGHT, "Good night scene" },
  { VOICE_SCENE_NIGHT, "Night mode" },
  { VOICE_SCENE_NIGHT, "Night scene" },
  { VOICE_SCENE_NIGHT, "Bedtime" },
  { VOICE_SCENE_NIGHT, "Bedtime mode" },

  // VOICE_SCENE_MORNING
  { VOICE_SCENE_MORNING, "Good morning" },
  { VOICE_SCENE_MORNING, "Morning mode" },
  { VOICE_SCENE_MORNING, "Morning scene" },
  { VOICE_SCENE_MORNING, "Start morning" },
  { VOICE_SCENE_MORNING, "Wake scene" },

  // VOICE_SCENE_SLEEP
  { VOICE_SCENE_SLEEP, "Sleep" },
  { VOICE_SCENE_SLEEP, "Sleep mode" },
  { VOICE_SCENE_SLEEP, "Sleep scene" },
  { VOICE_SCENE_SLEEP, "Go to sleep" },
  { VOICE_SCENE_SLEEP, "Sleep now" },
  { VOICE_SCENE_SLEEP, "Get sleepy" },

  // VOICE_WIFI_RESCAN
  { VOICE_WIFI_RESCAN, "Wifi rescan" },
  { VOICE_WIFI_RESCAN, "Rescan wifi" },
  { VOICE_WIFI_RESCAN, "Reconnect wifi" },
  { VOICE_WIFI_RESCAN, "Retry internet" },
  { VOICE_WIFI_RESCAN, "Find better wifi" },

  // VOICE_NETWORK_STATUS
  { VOICE_NETWORK_STATUS, "Wifi status" },
  { VOICE_NETWORK_STATUS, "Network status" },
  { VOICE_NETWORK_STATUS, "Internet status" },
  { VOICE_NETWORK_STATUS, "Show wifi status" },

  // VOICE_OFFLINE_ON
  { VOICE_OFFLINE_ON, "Offline mode on" },
  { VOICE_OFFLINE_ON, "Go offline" },
  { VOICE_OFFLINE_ON, "Enable offline mode" },

  // VOICE_OFFLINE_OFF
  { VOICE_OFFLINE_OFF, "Offline mode off" },
  { VOICE_OFFLINE_OFF, "Go online" },
  { VOICE_OFFLINE_OFF, "Disable offline mode" },

  // VOICE_EXHIBITION_ON
  { VOICE_EXHIBITION_ON, "Exhibition mode on" },
  { VOICE_EXHIBITION_ON, "Start exhibition mode" },
  { VOICE_EXHIBITION_ON, "Enable exhibition mode" },

  // VOICE_EXHIBITION_OFF
  { VOICE_EXHIBITION_OFF, "Exhibition mode off" },
  { VOICE_EXHIBITION_OFF, "Stop exhibition mode" },
  { VOICE_EXHIBITION_OFF, "Disable exhibition mode" },

  // VOICE_TIME
  { VOICE_TIME, "What time is it" },
  { VOICE_TIME, "Tell me the time" },
  { VOICE_TIME, "Current time" },

  // VOICE_DATE
  { VOICE_DATE, "What date is it" },
  { VOICE_DATE, "Tell me the date" },
  { VOICE_DATE, "What day is it" },

  // VOICE_SYSTEM_STATUS
  { VOICE_SYSTEM_STATUS, "System status" },
  { VOICE_SYSTEM_STATUS, "Status" },
  { VOICE_SYSTEM_STATUS, "What is your status" },

  // VOICE_MEMORY_STATUS
  { VOICE_MEMORY_STATUS, "Memory status" },
  { VOICE_MEMORY_STATUS, "Show memory" },
  { VOICE_MEMORY_STATUS, "Show ram" },

  // VOICE_BRAIN_STATUS
  { VOICE_BRAIN_STATUS, "Brain status" },
  { VOICE_BRAIN_STATUS, "Personal brain status" },

  // VOICE_STOPWATCH_START
  { VOICE_STOPWATCH_START, "Start stopwatch" },
  { VOICE_STOPWATCH_START, "Start counting" },
  { VOICE_STOPWATCH_START, "Time me" },

  // VOICE_STOPWATCH_STOP
  { VOICE_STOPWATCH_STOP, "Stop stopwatch" },
  { VOICE_STOPWATCH_STOP, "Stop the stopwatch" },
  { VOICE_STOPWATCH_STOP, "Stop counting" },

  // VOICE_STOPWATCH_RESET
  { VOICE_STOPWATCH_RESET, "Reset stopwatch" },
  { VOICE_STOPWATCH_RESET, "Reset the stopwatch" },
  { VOICE_STOPWATCH_RESET, "Clear stopwatch" },

  // VOICE_STOPWATCH_STATUS
  { VOICE_STOPWATCH_STATUS, "Stopwatch status" },
  { VOICE_STOPWATCH_STATUS, "How long has it been" },
  { VOICE_STOPWATCH_STATUS, "What is the stopwatch at" },

  // VOICE_TIMER_10S
  { VOICE_TIMER_10S, "Timer for ten seconds" },
  { VOICE_TIMER_10S, "Set timer for ten seconds" },
  { VOICE_TIMER_10S, "Ten second timer" },

  // VOICE_TIMER_30S
  { VOICE_TIMER_30S, "Timer for thirty seconds" },
  { VOICE_TIMER_30S, "Set timer for thirty seconds" },
  { VOICE_TIMER_30S, "Thirty second timer" },

  // VOICE_TIMER_1M
  { VOICE_TIMER_1M, "Timer for one minute" },
  { VOICE_TIMER_1M, "Set timer for one minute" },
  { VOICE_TIMER_1M, "One minute timer" },

  // VOICE_TIMER_2M
  { VOICE_TIMER_2M, "Timer for two minutes" },
  { VOICE_TIMER_2M, "Set timer for two minutes" },
  { VOICE_TIMER_2M, "Two minute timer" },

  // VOICE_TIMER_5M
  { VOICE_TIMER_5M, "Timer for five minutes" },
  { VOICE_TIMER_5M, "Set timer for five minutes" },
  { VOICE_TIMER_5M, "Five minute timer" },

  // VOICE_TIMER_10M
  { VOICE_TIMER_10M, "Timer for ten minutes" },
  { VOICE_TIMER_10M, "Set timer for ten minutes" },
  { VOICE_TIMER_10M, "Ten minute timer" },

  // VOICE_TIMER_15M
  { VOICE_TIMER_15M, "Timer for fifteen minutes" },
  { VOICE_TIMER_15M, "Set timer for fifteen minutes" },

  // VOICE_TIMER_20M
  { VOICE_TIMER_20M, "Timer for twenty minutes" },
  { VOICE_TIMER_20M, "Set timer for twenty minutes" },

  // VOICE_TIMER_30M
  { VOICE_TIMER_30M, "Timer for thirty minutes" },
  { VOICE_TIMER_30M, "Set timer for thirty minutes" },

  // VOICE_TIMER_1H
  { VOICE_TIMER_1H, "Timer for one hour" },
  { VOICE_TIMER_1H, "Set timer for one hour" },

  // VOICE_TIMER_PAUSE
  { VOICE_TIMER_PAUSE, "Pause timer" },
  { VOICE_TIMER_PAUSE, "Pause the timer" },

  // VOICE_TIMER_RESUME
  { VOICE_TIMER_RESUME, "Resume timer" },
  { VOICE_TIMER_RESUME, "Resume the timer" },
  { VOICE_TIMER_RESUME, "Continue timer" },

  // VOICE_TIMER_CANCEL
  { VOICE_TIMER_CANCEL, "Cancel timer" },
  { VOICE_TIMER_CANCEL, "Cancel the timer" },
  { VOICE_TIMER_CANCEL, "Stop timer" },

  // VOICE_TIMER_STATUS
  { VOICE_TIMER_STATUS, "Timer status" },
  { VOICE_TIMER_STATUS, "Time left" },
  { VOICE_TIMER_STATUS, "How much time is left" },

  // VOICE_ALARM_LIST
  { VOICE_ALARM_LIST, "List alarms" },
  { VOICE_ALARM_LIST, "Show alarms" },
  { VOICE_ALARM_LIST, "Alarm status" },
  { VOICE_ALARM_LIST, "What alarms are set" },

  // VOICE_ALARM_STOP
  { VOICE_ALARM_STOP, "Stop alarm" },
  { VOICE_ALARM_STOP, "Silence alarm" },
  { VOICE_ALARM_STOP, "Stop the alarm" },

  // VOICE_ALARM_CANCEL_ALL
  { VOICE_ALARM_CANCEL_ALL, "Cancel all alarms" },
  { VOICE_ALARM_CANCEL_ALL, "Clear all alarms" },
  { VOICE_ALARM_CANCEL_ALL, "Delete all alarms" },

  // VOICE_NOTES_LIST
  { VOICE_NOTES_LIST, "Show my notes" },
  { VOICE_NOTES_LIST, "Show notes" },
  { VOICE_NOTES_LIST, "List notes" },

  // VOICE_TASKS_LIST
  { VOICE_TASKS_LIST, "List tasks" },
  { VOICE_TASKS_LIST, "Show tasks" },
  { VOICE_TASKS_LIST, "Show my tasks" },

  // VOICE_TASKS_COMPLETED
  { VOICE_TASKS_COMPLETED, "Show completed tasks" },
  { VOICE_TASKS_COMPLETED, "List completed tasks" },

  // VOICE_ROUTINES_LIST
  { VOICE_ROUTINES_LIST, "List routines" },
  { VOICE_ROUTINES_LIST, "Show routines" },

  // VOICE_MEMORY_RECALL
  { VOICE_MEMORY_RECALL, "What do you remember about me" },
  { VOICE_MEMORY_RECALL, "What do you remember" },

  // VOICE_OFFLINE_KNOWLEDGE_STATUS
  { VOICE_OFFLINE_KNOWLEDGE_STATUS, "Offline knowledge status" },
  { VOICE_OFFLINE_KNOWLEDGE_STATUS, "Knowledge status" },
  { VOICE_OFFLINE_KNOWLEDGE_STATUS, "Cache status" },

  // VOICE_PENDING_QUERIES
  { VOICE_PENDING_QUERIES, "Show pending queries" },
  { VOICE_PENDING_QUERIES, "Pending queries" },
  { VOICE_PENDING_QUERIES, "Show pending questions" },

  // VOICE_QUIZ_SCORE
  { VOICE_QUIZ_SCORE, "Quiz score" },
  { VOICE_QUIZ_SCORE, "Show quiz score" },

  // VOICE_FREEFORM_STT - Phase 5J
  // Five aliases use five of the ten remaining MultiNet phrase slots.
  { VOICE_FREEFORM_STT, "Ask Elli" },
  { VOICE_FREEFORM_STT, "Ask a question" },
  { VOICE_FREEFORM_STT, "Online question" },
  { VOICE_FREEFORM_STT, "Free speech" },
  { VOICE_FREEFORM_STT, "Talk to Elli" },

  // VOICE_STOP
  { VOICE_STOP, "Stop" },
  { VOICE_STOP, "Stop now" },
  { VOICE_STOP, "Cancel" },
};

constexpr size_t VOICE_PHRASE_COUNT =
  sizeof(voiceCommands) / sizeof(voiceCommands[0]);

static_assert(
  VOICE_PHRASE_COUNT <= 200,
  "Too many ESP-SR MultiNet command phrases; maximum is 200."
);


// ============================================================
// QUEUE / STATE
// ============================================================

QueueHandle_t voiceQueue = nullptr;

bool voiceReadyFlag = false;
bool voicePausedForCommand = false;
int currentVoiceCommand = -1;

// ============================================================
// PHASE 5K.3 - SPEECH-END AUTOMATIC STT FALLBACK
// ============================================================
// Never cut a question at a fixed time.
// Known MultiNet commands still match locally as usual.
// Unknown/free-form speech moves to STT only after real speech was heard
// and then ~1.1 seconds of actual silence followed it.
// ============================================================

constexpr uint32_t AUTO_STT_MIN_LISTEN_MS = 900;
constexpr uint32_t AUTO_STT_END_SILENCE_MS = 1100;

volatile bool commandWatchdogArmed = false;
volatile uint32_t commandListenStartMs = 0;

// KIRA Next Group 1:
// ESP-SR/MultiNet may time out while the person is still talking.
// AudioInput keeps recording independently until VAD confirms speech end.
volatile bool srTimedOutWaitingForSpeechEnd = false;


// ============================================================
// COMMAND -> UNIVERSAL BRAIN TEXT
// ============================================================

bool voiceCommandToText(
  int commandId,
  String& output
) {

  switch(commandId) {

    case VOICE_HELLO:
      output = "hello elli";
      return true;

    case VOICE_LIGHT_ON:
      output = "turn on the light";
      return true;

    case VOICE_LIGHT_OFF:
      output = "turn off the light";
      return true;

    case VOICE_FAN_ON:
      output = "turn on the fan";
      return true;

    case VOICE_FAN_OFF:
      output = "turn off the fan";
      return true;

    case VOICE_CHARGER_ON:
      output = "turn on the charger";
      return true;

    case VOICE_CHARGER_OFF:
      output = "turn off the charger";
      return true;

    case VOICE_SECOND_LIGHT_ON:
      output = "second light on";
      return true;

    case VOICE_SECOND_LIGHT_OFF:
      output = "second light off";
      return true;

    case VOICE_ALL_ON:
      output = "all devices on";
      return true;

    case VOICE_ALL_OFF:
      output = "all devices off";
      return true;

    case VOICE_SCENE_STUDY:
      output = "study scene";
      return true;

    case VOICE_SCENE_LEAVING:
      output = "leaving scene";
      return true;

    case VOICE_SCENE_NIGHT:
      output = "good night scene";
      return true;

    case VOICE_SCENE_MORNING:
      output = "morning scene";
      return true;

    case VOICE_SCENE_SLEEP:
      output = "sleep scene";
      return true;

    case VOICE_WIFI_RESCAN:
      output = "wifi rescan";
      return true;

    case VOICE_NETWORK_STATUS:
      output = "wifi status";
      return true;

    case VOICE_OFFLINE_ON:
      output = "offline mode on";
      return true;

    case VOICE_OFFLINE_OFF:
      output = "offline mode off";
      return true;

    case VOICE_EXHIBITION_ON:
      output = "exhibition mode on";
      return true;

    case VOICE_EXHIBITION_OFF:
      output = "exhibition mode off";
      return true;

    case VOICE_TIME:
      output = "what time is it";
      return true;

    case VOICE_DATE:
      output = "what date is it";
      return true;

    case VOICE_SYSTEM_STATUS:
      output = "system status";
      return true;

    case VOICE_MEMORY_STATUS:
      output = "memory status";
      return true;

    case VOICE_BRAIN_STATUS:
      output = "brain status";
      return true;

    case VOICE_STOPWATCH_START:
      output = "start stopwatch";
      return true;

    case VOICE_STOPWATCH_STOP:
      output = "stop stopwatch";
      return true;

    case VOICE_STOPWATCH_RESET:
      output = "reset stopwatch";
      return true;

    case VOICE_STOPWATCH_STATUS:
      output = "stopwatch status";
      return true;

    case VOICE_TIMER_10S:
      output = "set timer for 10 seconds";
      return true;

    case VOICE_TIMER_30S:
      output = "set timer for 30 seconds";
      return true;

    case VOICE_TIMER_1M:
      output = "set timer for 1 minute";
      return true;

    case VOICE_TIMER_2M:
      output = "set timer for 2 minutes";
      return true;

    case VOICE_TIMER_5M:
      output = "set timer for 5 minutes";
      return true;

    case VOICE_TIMER_10M:
      output = "set timer for 10 minutes";
      return true;

    case VOICE_TIMER_15M:
      output = "set timer for 15 minutes";
      return true;

    case VOICE_TIMER_20M:
      output = "set timer for 20 minutes";
      return true;

    case VOICE_TIMER_30M:
      output = "set timer for 30 minutes";
      return true;

    case VOICE_TIMER_1H:
      output = "set timer for 1 hour";
      return true;

    case VOICE_TIMER_PAUSE:
      output = "pause timer";
      return true;

    case VOICE_TIMER_RESUME:
      output = "resume timer";
      return true;

    case VOICE_TIMER_CANCEL:
      output = "cancel timer";
      return true;

    case VOICE_TIMER_STATUS:
      output = "timer status";
      return true;

    case VOICE_ALARM_LIST:
      output = "list alarms";
      return true;

    case VOICE_ALARM_STOP:
      output = "stop alarm";
      return true;

    case VOICE_ALARM_CANCEL_ALL:
      output = "cancel all alarms";
      return true;

    case VOICE_NOTES_LIST:
      output = "show my notes";
      return true;

    case VOICE_TASKS_LIST:
      output = "list tasks";
      return true;

    case VOICE_TASKS_COMPLETED:
      output = "show completed tasks";
      return true;

    case VOICE_ROUTINES_LIST:
      output = "list routines";
      return true;

    case VOICE_MEMORY_RECALL:
      output = "what do you remember about me";
      return true;

    case VOICE_OFFLINE_KNOWLEDGE_STATUS:
      output = "offline knowledge status";
      return true;

    case VOICE_PENDING_QUERIES:
      output = "show pending queries";
      return true;

    case VOICE_QUIZ_SCORE:
      output = "quiz score";
      return true;

    case VOICE_STOP:
      output = "stop";
      return true;

    default:
      output = "";
      return false;
  }
}


// ============================================================
// SPEAKER ACKNOWLEDGEMENT
// ============================================================

constexpr int SPEAKER_FRAMES = 128;

int16_t speakerBuffer[SPEAKER_FRAMES * 2];
float speakerPhase = 0.0f;


void playToneHz(
  float frequency,
  uint32_t durationMs
) {

  const float phaseStep =
    (float)TWO_PI *
    frequency /
    (float)SPK_SAMPLE_RATE;

  uint32_t start = millis();

  while(
    millis() - start <
    durationMs
  ) {

    for(
      int i = 0;
      i < SPEAKER_FRAMES;
      i++
    ) {

      int16_t sample =
        (int16_t)(
          sinf(speakerPhase) *
          SPEAKER_VOLUME
        );

      speakerBuffer[i * 2]     = sample;
      speakerBuffer[i * 2 + 1] = sample;

      speakerPhase += phaseStep;

      if(
        speakerPhase >=
        (float)TWO_PI
      ) {

        speakerPhase -=
          (float)TWO_PI;
      }
    }

    SpeakerI2S.write(
      (uint8_t*)speakerBuffer,
      sizeof(speakerBuffer)
    );
  }
}


void sendSpeakerSilence() {

  memset(
    speakerBuffer,
    0,
    sizeof(speakerBuffer)
  );

  SpeakerI2S.write(
    (uint8_t*)speakerBuffer,
    sizeof(speakerBuffer)
  );
}


void soundUp() {

  playToneHz(520.0f, 110);
  delay(35);
  playToneHz(760.0f, 150);
}


void soundDown() {

  playToneHz(760.0f, 110);
  delay(35);
  playToneHz(480.0f, 150);
}


void soundHello() {

  playToneHz(600.0f, 70);
  delay(30);
  playToneHz(760.0f, 70);
  delay(30);
  playToneHz(900.0f, 100);
}


void soundScene() {

  playToneHz(660.0f, 90);
  delay(25);
  playToneHz(830.0f, 90);
}


void soundGeneric() {

  playToneHz(650.0f, 130);
}


void playAcknowledgement(
  int commandId
) {

  digitalWrite(
    AMP_SD,
    HIGH
  );

  delay(60);

  switch(commandId) {

    case VOICE_LIGHT_ON:
    case VOICE_FAN_ON:
    case VOICE_ALL_ON:
      soundUp();
      break;

    case VOICE_LIGHT_OFF:
    case VOICE_FAN_OFF:
    case VOICE_ALL_OFF:
      soundDown();
      break;

    case VOICE_HELLO:
      soundHello();
      break;

    case VOICE_SCENE_STUDY:
    case VOICE_SCENE_LEAVING:
    case VOICE_SCENE_NIGHT:
    case VOICE_SCENE_MORNING:
    case VOICE_SCENE_SLEEP:
      soundScene();
      break;

    default:
      soundGeneric();
      break;
  }

  sendSpeakerSilence();

  delay(25);

  digitalWrite(
    AMP_SD,
    LOW
  );
}


// ============================================================
// PHASE 5J / 5J-2 - FREE-FORM ONLINE SPEECH TO TEXT
// ============================================================
//
// SAFE/GATED V1 DESIGN:
//
//   Hi ESP
//      -> "Ask Elli"
//      -> short listening beep
//      -> speak one normal sentence
//      -> WAV captured to PSRAM
//      -> Groq Whisper transcription
//      -> ordinary text goes into Universal processCommand()
//
// Why gated instead of uploading every wake-word interaction:
// - all 190+ offline MultiNet phrases stay fast and local
// - network traffic happens only when explicitly requested
// - this is safer for the current power-stability baseline
//
// If usable internet or a Groq key is unavailable, Phase 5J does not
// attempt an upload and the normal offline command system remains intact.
// ============================================================

constexpr uint32_t STT_SAMPLE_RATE = 16000;
constexpr uint16_t STT_BITS_PER_SAMPLE = 16;
constexpr uint16_t STT_CHANNELS = 1;

// Hard cap protects RAM and limits Wi-Fi upload size.
constexpr uint32_t STT_MAX_RECORD_MS = 6500;
constexpr uint32_t STT_START_TIMEOUT_MS = 2500;
constexpr uint32_t STT_END_SILENCE_MS = 850;
constexpr uint32_t STT_MIN_SPEECH_MS = 180;

// 20 ms at 16 kHz.
constexpr size_t STT_FRAME_SAMPLES = 320;

// Tuned conservatively for the current INMP441 + ESP-SR gain setup.
// Serial prints the measured level so we can tune these later if needed.
constexpr uint32_t STT_SPEECH_LEVEL = 420;
constexpr uint32_t STT_SILENCE_LEVEL = 280;

bool groqSttConfigured() {
  return strlen(KIRA_GROQ_API_KEY) > 20;
}

void writeLe16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void writeLe32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void makeWavHeader(uint8_t header[44], uint32_t pcmBytes) {
  memset(header, 0, 44);

  memcpy(header + 0, "RIFF", 4);
  writeLe32(header + 4, 36UL + pcmBytes);
  memcpy(header + 8, "WAVE", 4);

  memcpy(header + 12, "fmt ", 4);
  writeLe32(header + 16, 16);
  writeLe16(header + 20, 1); // PCM
  writeLe16(header + 22, STT_CHANNELS);
  writeLe32(header + 24, STT_SAMPLE_RATE);
  writeLe32(
    header + 28,
    STT_SAMPLE_RATE * STT_CHANNELS * (STT_BITS_PER_SAMPLE / 8)
  );
  writeLe16(
    header + 32,
    STT_CHANNELS * (STT_BITS_PER_SAMPLE / 8)
  );
  writeLe16(header + 34, STT_BITS_PER_SAMPLE);

  memcpy(header + 36, "data", 4);
  writeLe32(header + 40, pcmBytes);
}

uint32_t meanAbsLevel(const int16_t* data, size_t samples) {
  if (!data || samples == 0) return 0;

  uint64_t sum = 0;

  for (size_t i = 0; i < samples; i++) {
    int32_t v = data[i];
    if (v < 0) v = -v;
    sum += (uint32_t)v;
  }

  return (uint32_t)(sum / samples);
}


uint32_t meanAbsDifferenceLevel(
  const int16_t* data,
  size_t samples
) {
  if(!data || samples<2) return 0;

  uint64_t sum=0;

  for(size_t i=1;i<samples;i++){
    int32_t d=(int32_t)data[i]-(int32_t)data[i-1];
    if(d<0) d=-d;
    sum+=(uint32_t)d;
  }

  return (uint32_t)(sum/(samples-1));
}

String stripFreeformWakePrefix(String text) {
  text.trim();
  if (!text.length()) return text;

  String lower = text;
  lower.toLowerCase();
  lower.trim();

  const char* const wakePhrases[] = {
    "hi esp",
    "hey esp",
    "hello esp",
    "hey elli",
    "hi elli",
    "hello elli",
    "okay elli",
    "ok elli"
  };

  for (size_t i = 0; i < sizeof(wakePhrases) / sizeof(wakePhrases[0]); i++) {
    String wake = wakePhrases[i];

    if (!lower.startsWith(wake)) {
      continue;
    }

    if (lower.length() == wake.length()) {
      return "";
    }

    char next = lower[wake.length()];

    if (
      next == ' ' ||
      next == ',' ||
      next == '.' ||
      next == ':' ||
      next == ';' ||
      next == '-' ||
      next == '!' ||
      next == '?'
    ) {
      text.remove(0, wake.length());

      while (text.length()) {
        char c = text[0];
        if (
          c == ' ' ||
          c == ',' ||
          c == '.' ||
          c == ':' ||
          c == ';' ||
          c == '-' ||
          c == '!' ||
          c == '?'
        ) {
          text.remove(0, 1);
        }
        else {
          break;
        }
      }

      text.trim();
      return text;
    }
  }

  return text;
}

bool captureSttPcm(int16_t*& pcm, size_t& samplesCaptured) {
  pcm = nullptr;
  samplesCaptured = 0;

  const size_t maxSamples =
    (size_t)STT_SAMPLE_RATE * STT_MAX_RECORD_MS / 1000UL;

  const size_t maxBytes = maxSamples * sizeof(int16_t);

  pcm = (int16_t*)heap_caps_malloc(
    maxBytes,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (!pcm) {
    Serial.println("[VOICE/STT] ERROR: PSRAM audio allocation failed");
    return false;
  }

  int16_t frame[STT_FRAME_SAMPLES];

  bool speechStarted = false;
  uint32_t speechStartMs = 0;
  uint32_t lastSpeechMs = 0;
  uint32_t captureStartMs = millis();
  uint32_t maxLevel = 0;

  // The ESP-SR feed task is already paused before this function runs.
  // Give its I2S read a moment to release, then capture directly.
  delay(35);
  MicI2S.setTimeout(250);

  Serial.println("[VOICE/STT] Listening for free-form speech...");

  while (millis() - captureStartMs < STT_MAX_RECORD_MS) {
    size_t bytesRead = MicI2S.readBytes(
      (char*)frame,
      sizeof(frame)
    );

    if (bytesRead < sizeof(int16_t)) {
      continue;
    }

    size_t frameSamples = bytesRead / sizeof(int16_t);

    if (samplesCaptured + frameSamples > maxSamples) {
      frameSamples = maxSamples - samplesCaptured;
    }

    memcpy(
      pcm + samplesCaptured,
      frame,
      frameSamples * sizeof(int16_t)
    );

    samplesCaptured += frameSamples;

    uint32_t level = meanAbsLevel(frame, frameSamples);
    if (level > maxLevel) maxLevel = level;

    uint32_t now = millis();

    if (!speechStarted) {
      if (level >= STT_SPEECH_LEVEL) {
        speechStarted = true;
        speechStartMs = now;
        lastSpeechMs = now;
        Serial.print("[VOICE/STT] Speech detected, level ");
        Serial.println(level);
      }
      else if (now - captureStartMs >= STT_START_TIMEOUT_MS) {
        Serial.println("[VOICE/STT] No speech detected after prompt");
        break;
      }
    }
    else {
      if (level >= STT_SILENCE_LEVEL) {
        lastSpeechMs = now;
      }

      bool enoughSpeech =
        now - speechStartMs >= STT_MIN_SPEECH_MS;

      if (
        enoughSpeech &&
        now - lastSpeechMs >= STT_END_SILENCE_MS
      ) {
        Serial.println("[VOICE/STT] End of sentence detected");
        break;
      }
    }

    if (samplesCaptured >= maxSamples) break;

    // Keep the display task and RTOS scheduler responsive.
    delay(1);
  }

  Serial.print("[VOICE/STT] Captured samples: ");
  Serial.println(samplesCaptured);
  Serial.print("[VOICE/STT] Peak mean level: ");
  Serial.println(maxLevel);

  if (!speechStarted) {
    heap_caps_free(pcm);
    pcm = nullptr;
    samplesCaptured = 0;
    return false;
  }

  // Avoid sending an extremely tiny accidental click/noise capture.
  const size_t minUsefulSamples = STT_SAMPLE_RATE / 2;

  if (samplesCaptured < minUsefulSamples) {
    heap_caps_free(pcm);
    pcm = nullptr;
    samplesCaptured = 0;
    Serial.println("[VOICE/STT] Capture too short");
    return false;
  }

  return true;
}


bool sttTlsWriteAll(
  WiFiClientSecure& client,
  const uint8_t* data,
  size_t bytes
) {
  if (data == nullptr && bytes != 0) return false;

  size_t sent = 0;
  uint32_t lastProgressMs = millis();

  while (sent < bytes) {
    if (!client.connected()) {
      Serial.println("[VOICE/STT] TLS connection lost while uploading");
      return false;
    }

    size_t n = client.write(data + sent, bytes - sent);

    if (n > 0) {
      sent += n;
      lastProgressMs = millis();
      continue;
    }

    if (millis() - lastProgressMs > 7000) {
      Serial.println("[VOICE/STT] TLS upload stalled");
      return false;
    }

    delay(2);
  }

  return true;
}


bool sttTlsWritePsramPcm(
  WiFiClientSecure& client,
  const int16_t* pcm,
  size_t pcmBytes
) {
  if (!pcm || pcmBytes == 0) return false;

  // Copy PSRAM audio into a small internal-RAM staging block before TLS.
  constexpr size_t TX_CHUNK = 2048;
  uint8_t tx[TX_CHUNK];

  const uint8_t* source =
    reinterpret_cast<const uint8_t*>(pcm);

  size_t offset = 0;

  while (offset < pcmBytes) {
    size_t n = pcmBytes - offset;
    if (n > TX_CHUNK) n = TX_CHUNK;

    memcpy(tx, source + offset, n);

    if (!sttTlsWriteAll(client, tx, n)) {
      return false;
    }

    offset += n;

    if ((offset & 0x3FFF) == 0) {
      delay(1);
    }
  }

  return true;
}


bool sttReadExact(
  WiFiClientSecure& client,
  uint8_t* out,
  size_t bytes,
  uint32_t timeoutMs
) {
  size_t got = 0;
  uint32_t lastDataMs = millis();

  while (got < bytes) {
    int available = client.available();

    if (available > 0) {
      size_t want = bytes - got;
      if (want > (size_t)available) want = (size_t)available;

      int n = client.read(out + got, want);

      if (n > 0) {
        got += (size_t)n;
        lastDataMs = millis();
        continue;
      }
    }

    if (!client.connected() && client.available() <= 0) {
      return false;
    }

    if (millis() - lastDataMs > timeoutMs) {
      return false;
    }

    delay(1);
  }

  return true;
}


void sttAppendResponse(
  String& response,
  const uint8_t* data,
  size_t bytes
) {
  constexpr size_t RESPONSE_CAP = 4096;

  if (response.length() >= RESPONSE_CAP) return;

  size_t room = RESPONSE_CAP - response.length();
  if (bytes > room) bytes = room;

  response.concat(
    reinterpret_cast<const char*>(data),
    (unsigned int)bytes
  );
}


bool sttReadHttpResponse(
  WiFiClientSecure& client,
  int& statusCode,
  String& response
) {
  statusCode = -1;
  response = "";

  // Whisper needs time to process the uploaded WAV before the first
  // HTTP response byte arrives. The previous 20 ms timeout was far
  // too short and produced an empty "Invalid HTTP status line".
  constexpr uint32_t STT_RESPONSE_START_TIMEOUT_MS = 20000;
  constexpr uint32_t STT_RESPONSE_STREAM_TIMEOUT_MS = 15000;

  client.setTimeout(
    STT_RESPONSE_STREAM_TIMEOUT_MS
  );

  uint32_t waitStartMs =
    millis();

  while(
    client.available() <= 0 &&
    client.connected() &&
    millis() - waitStartMs <
      STT_RESPONSE_START_TIMEOUT_MS
  ) {
    delay(2);
  }

  if(
    client.available() <= 0
  ) {
    Serial.print(
      "[VOICE/STT] No HTTP response bytes after "
    );
    Serial.print(
      millis() - waitStartMs
    );
    Serial.println(
      " ms"
    );
    return false;
  }

  Serial.print(
    "[VOICE/STT] First response byte after "
  );
  Serial.print(
    millis() - waitStartMs
  );
  Serial.println(
    " ms"
  );

  String statusLine =
    client.readStringUntil(
      '\n'
    );

  statusLine.trim();

  if(
    !statusLine.startsWith(
      "HTTP/"
    )
  ) {
    Serial.print(
      "[VOICE/STT] Invalid HTTP status line: "
    );
    Serial.println(
      statusLine
    );
    return false;
  }

  int firstSpace = statusLine.indexOf(' ');
  if (firstSpace < 0) return false;

  statusCode =
    statusLine.substring(
      firstSpace + 1
    ).toInt();

  Serial.print(
    "[VOICE/STT] Status line: "
  );
  Serial.println(
    statusLine
  );

  int contentLength = -1;
  bool chunked = false;

  while (true) {
    String line = client.readStringUntil('\n');
    line.trim();

    if (!line.length()) break;

    String lower = line;
    lower.toLowerCase();

    if (lower.startsWith("content-length:")) {
      String value = line.substring(line.indexOf(':') + 1);
      value.trim();
      contentLength = value.toInt();
    }

    if (
      lower.startsWith("transfer-encoding:") &&
      lower.indexOf("chunked") >= 0
    ) {
      chunked = true;
    }
  }

  uint8_t rx[256];

  if (chunked) {
    while (true) {
      String sizeLine = client.readStringUntil('\n');
      sizeLine.trim();

      int semicolon = sizeLine.indexOf(';');
      if (semicolon >= 0) sizeLine.remove(semicolon);

      unsigned long chunkBytes =
        strtoul(sizeLine.c_str(), nullptr, 16);

      if (chunkBytes == 0) {
        while (true) {
          String trailer = client.readStringUntil('\n');
          trailer.trim();
          if (!trailer.length()) break;
        }
        break;
      }

      unsigned long remaining = chunkBytes;

      while (remaining > 0) {
        size_t want =
          remaining > sizeof(rx)
            ? sizeof(rx)
            : (size_t)remaining;

        if (!sttReadExact(client, rx, want, 15000)) {
          return false;
        }

        sttAppendResponse(response, rx, want);
        remaining -= want;
      }

      uint8_t crlf[2];
      if (!sttReadExact(client, crlf, sizeof(crlf), 5000)) {
        return false;
      }
    }

    return true;
  }

  if (contentLength >= 0) {
    size_t remaining = (size_t)contentLength;

    while (remaining > 0) {
      size_t want =
        remaining > sizeof(rx)
          ? sizeof(rx)
          : remaining;

      if (!sttReadExact(client, rx, want, 15000)) {
        return false;
      }

      sttAppendResponse(response, rx, want);
      remaining -= want;
    }

    return true;
  }

  uint32_t lastDataMs = millis();

  while (client.connected() || client.available() > 0) {
    int available = client.available();

    if (available > 0) {
      size_t want = (size_t)available;
      if (want > sizeof(rx)) want = sizeof(rx);

      int n = client.read(rx, want);

      if (n > 0) {
        sttAppendResponse(response, rx, (size_t)n);
        lastDataMs = millis();
        continue;
      }
    }

    if (millis() - lastDataMs > 5000) break;
    delay(1);
  }

  return true;
}


bool groqSttUploadOnce(
  const int16_t* pcm,
  size_t samples,
  const String& boundary,
  const String& preamble,
  const String& ending,
  int& statusCode,
  String& response
) {
  statusCode = -1;
  response = "";

  KiraWifiRequestScope netScope(
    KIRA_NET_PRIORITY_STT,
    "Groq STT"
  );

  const uint32_t pcmBytes =
    (uint32_t)(samples * sizeof(int16_t));

  const size_t bodyBytes =
    preamble.length() +
    44 +
    pcmBytes +
    ending.length();

  Serial.print("[VOICE/STT] Streaming upload size: ");
  Serial.print(bodyBytes);
  Serial.println(" bytes");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);

  if (!client.connect("api.groq.com", 443)) {
    Serial.println("[VOICE/STT] TLS connect failed");
    return false;
  }

  String requestHead;
  requestHead.reserve(520);

  requestHead += "POST /openai/v1/audio/transcriptions HTTP/1.1\r\n";
  requestHead += "Host: api.groq.com\r\n";
  requestHead += "Authorization: Bearer ";
  requestHead += KIRA_GROQ_API_KEY;
  requestHead += "\r\n";
  requestHead += "Content-Type: multipart/form-data; boundary=";
  requestHead += boundary;
  requestHead += "\r\n";
  requestHead += "Content-Length: ";
  requestHead += String(bodyBytes);
  requestHead += "\r\n";
  requestHead += "Accept: text/plain\r\n";
  requestHead += "Accept-Encoding: identity\r\n";
  requestHead += "Connection: close\r\n\r\n";

  if (
    !sttTlsWriteAll(
      client,
      reinterpret_cast<const uint8_t*>(requestHead.c_str()),
      requestHead.length()
    )
  ) {
    client.stop();
    return false;
  }

  if (
    !sttTlsWriteAll(
      client,
      reinterpret_cast<const uint8_t*>(preamble.c_str()),
      preamble.length()
    )
  ) {
    client.stop();
    return false;
  }

  uint8_t wavHeader[44];
  makeWavHeader(wavHeader, pcmBytes);

  if (!sttTlsWriteAll(client, wavHeader, sizeof(wavHeader))) {
    client.stop();
    return false;
  }

  if (!sttTlsWritePsramPcm(client, pcm, pcmBytes)) {
    client.stop();
    return false;
  }

  if (
    !sttTlsWriteAll(
      client,
      reinterpret_cast<const uint8_t*>(ending.c_str()),
      ending.length()
    )
  ) {
    client.stop();
    return false;
  }

  Serial.println(
    "[VOICE/STT] Upload complete; waiting for transcription"
  );

  bool readOk =
    sttReadHttpResponse(client, statusCode, response);


  netScope.setHttpCode(
    statusCode
  );

  netScope.setSuccess(
    readOk &&
    statusCode>=200 &&
    statusCode<300
  );


  client.stop();
  return readOk;
}


bool groqTranscribePcm(
  const int16_t* pcm,
  size_t samples,
  String& transcript
) {
  transcript = "";

  if (!pcm || samples == 0) return false;

  if (!groqSttConfigured()) {
    Serial.println("[VOICE/STT] Groq STT key is not configured");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[VOICE/STT] Wi-Fi disconnected; upload skipped");
    return false;
  }

  const String boundary = "----KIRASTT5JBoundary";

  String preamble;
  preamble.reserve(760);

  preamble += "--" + boundary + "\r\n";
  preamble +=
    "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  preamble += "whisper-large-v3-turbo\r\n";

  preamble += "--" + boundary + "\r\n";
  preamble +=
    "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
  preamble += "en\r\n";

  preamble += "--" + boundary + "\r\n";
  preamble +=
    "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n";
  preamble += "text\r\n";

  preamble += "--" + boundary + "\r\n";
  preamble +=
    "Content-Disposition: form-data; name=\"prompt\"\r\n\r\n";
  preamble +=
    "KIRA, Elli, ESP32-S3, neuromorphic computing, light, fan, timer, alarm, study mode, leaving mode.\r\n";

  preamble += "--" + boundary + "\r\n";
  preamble +=
    "Content-Disposition: form-data; name=\"file\"; filename=\"kira.wav\"\r\n";
  preamble += "Content-Type: audio/wav\r\n\r\n";

  String ending =
    "\r\n--" + boundary + "--\r\n";

  elliVisualSetState(ELLI_VISUAL_THINKING);

  int code = -1;
  String response;
  bool requestOk = false;

  for (uint8_t attempt = 1; attempt <= 2; attempt++) {
    Serial.print("[VOICE/STT] Upload attempt ");
    Serial.print(attempt);
    Serial.println("/2");

    requestOk =
      groqSttUploadOnce(
        pcm,
        samples,
        boundary,
        preamble,
        ending,
        code,
        response
      );

    if (requestOk) break;

    Serial.println("[VOICE/STT] Upload transport failed");

    if (attempt < 2) {
      Serial.print("[VOICE/STT] Retrying; Wi-Fi RSSI=");
      Serial.println(WiFi.RSSI());
      delay(350);
    }
  }

  Serial.print("[VOICE/STT] HTTP ");
  Serial.println(code);

  if (!requestOk || code < 200 || code >= 300) {
    if (response.length()) {
      String diagnostic = response;
      diagnostic.trim();

      if (diagnostic.length() > 220) {
        diagnostic.remove(220);
      }

      Serial.print("[VOICE/STT] Provider response: ");
      Serial.println(diagnostic);
    }

    Serial.println("[VOICE/STT] Transcription request failed");
    return false;
  }

  response.trim();

  if (response.startsWith("{")) {
    int key = response.indexOf("\"text\"");

    if (key >= 0) {
      int colon = response.indexOf(':', key);
      int firstQuote = response.indexOf('"', colon + 1);
      int secondQuote =
        firstQuote >= 0
          ? response.indexOf('"', firstQuote + 1)
          : -1;

      if (firstQuote >= 0 && secondQuote > firstQuote) {
        response =
          response.substring(firstQuote + 1, secondQuote);
      }
    }
  }

  response = stripFreeformWakePrefix(response);
  response.trim();

  if (!response.length()) {
    Serial.println("[VOICE/STT] Empty transcript");
    return false;
  }

  transcript = response;

  Serial.print("[VOICE/STT] Transcript: ");
  Serial.println(transcript);

  return true;
}


// ============================================================
// PHASE 5K + 5J-2 - ADAPTIVE CAPTURE CLEANUP
// ============================================================
//
// MultiNet's timeout occurs after the user has already spoken. The
// KIRA_SR bridge preserves that audio. Before upload we estimate the
// ambient floor, locate a real speech region, keep 240 ms of pre-roll,
// trim long trailing silence, and reject tiny click/noise bursts.
// ============================================================

bool selectAdaptiveSpeechWindow(
  const int16_t* pcm,
  size_t samples,
  size_t& startSample,
  size_t& windowSamples
) {
  startSample=0;
  windowSamples=0;

  if(!pcm || samples<STT_FRAME_SAMPLES*6) return false;

  const size_t frameCount=samples/STT_FRAME_SAMPLES;
  if(frameCount==0) return false;

  constexpr size_t QUIET_SLOTS=12;
  uint32_t quietEnergy[QUIET_SLOTS];
  uint32_t quietDiff[QUIET_SLOTS];

  for(size_t i=0;i<QUIET_SLOTS;i++){
    quietEnergy[i]=0xFFFFFFFFUL;
    quietDiff[i]=0xFFFFFFFFUL;
  }

  uint32_t peakLevel=0;
  uint32_t peakDiff=0;

  for(size_t f=0;f<frameCount;f++){
    const int16_t* frame=pcm+f*STT_FRAME_SAMPLES;

    uint32_t level=meanAbsLevel(frame,STT_FRAME_SAMPLES);
    uint32_t diff=meanAbsDifferenceLevel(frame,STT_FRAME_SAMPLES);

    if(level>peakLevel) peakLevel=level;
    if(diff>peakDiff) peakDiff=diff;

    if(level<quietEnergy[QUIET_SLOTS-1]){
      size_t pos=QUIET_SLOTS-1;
      quietEnergy[pos]=level;

      while(pos>0 && quietEnergy[pos]<quietEnergy[pos-1]){
        uint32_t t=quietEnergy[pos-1];
        quietEnergy[pos-1]=quietEnergy[pos];
        quietEnergy[pos]=t;
        pos--;
      }
    }

    if(diff<quietDiff[QUIET_SLOTS-1]){
      size_t pos=QUIET_SLOTS-1;
      quietDiff[pos]=diff;

      while(pos>0 && quietDiff[pos]<quietDiff[pos-1]){
        uint32_t t=quietDiff[pos-1];
        quietDiff[pos-1]=quietDiff[pos];
        quietDiff[pos]=t;
        pos--;
      }
    }
  }

  uint64_t energySum=0;
  uint64_t diffSum=0;
  size_t energyCount=0;
  size_t diffCount=0;

  for(size_t i=0;i<QUIET_SLOTS;i++){
    if(quietEnergy[i]!=0xFFFFFFFFUL){
      energySum+=quietEnergy[i];
      energyCount++;
    }

    if(quietDiff[i]!=0xFFFFFFFFUL){
      diffSum+=quietDiff[i];
      diffCount++;
    }
  }

  if(energyCount==0 || diffCount==0) return false;

  uint32_t noiseFloor=(uint32_t)(energySum/energyCount);
  uint32_t diffFloor=(uint32_t)(diffSum/diffCount);

  uint32_t speechThreshold=noiseFloor*2UL+120UL;
  if(speechThreshold<320UL) speechThreshold=320UL;
  if(speechThreshold>5000UL) speechThreshold=5000UL;

  uint32_t activeThreshold=noiseFloor+noiseFloor/2UL+100UL;
  if(activeThreshold<300UL) activeThreshold=300UL;

  uint32_t diffSpeechThreshold=diffFloor*2UL+70UL;
  if(diffSpeechThreshold<100UL) diffSpeechThreshold=100UL;

  uint32_t diffActiveThreshold=diffFloor+diffFloor/2UL+45UL;
  if(diffActiveThreshold<75UL) diffActiveThreshold=75UL;

  uint32_t voicePeakThreshold=noiseFloor*5UL/2UL+180UL;
  if(voicePeakThreshold<500UL) voicePeakThreshold=500UL;

  Serial.print("[VOICE/VAD V2.4] noise=");
  Serial.print(noiseFloor);
  Serial.print(" diff_noise=");
  Serial.print(diffFloor);
  Serial.print(" energy_start=");
  Serial.print(speechThreshold);
  Serial.print(" diff_start=");
  Serial.print(diffSpeechThreshold);
  Serial.print(" peak=");
  Serial.print(peakLevel);
  Serial.print(" diff_peak=");
  Serial.println(peakDiff);

  if(peakLevel<voicePeakThreshold){
    Serial.println(
      "[VOICE/VAD V2.4] Rejected as steady room/fan noise - NO STT request"
    );
    return false;
  }

  size_t firstSpeechFrame=frameCount;
  size_t lastActiveFrame=0;
  size_t strongFrames=0;
  size_t activeFrames=0;
  size_t silentRun=0;
  bool speechStarted=false;

  constexpr size_t END_SILENCE_FRAMES=16; // 320ms trim silence

  for(size_t f=0;f<frameCount;f++){
    const int16_t* frame=pcm+f*STT_FRAME_SAMPLES;

    uint32_t level=meanAbsLevel(frame,STT_FRAME_SAMPLES);
    uint32_t diff=meanAbsDifferenceLevel(frame,STT_FRAME_SAMPLES);

    uint32_t energyRatio=
      (level*100UL)/
      (noiseFloor+1UL);

    uint32_t localDiffRatio=
      (diff*100UL)/
      (diffFloor+1UL);

    // V2.4 upload trim:
    // do not require energy and spectral activity to peak in the SAME frame.
    // Human speech can have vowel-heavy frames (high energy) and consonant
    // frames (high difference) at slightly different moments.
    bool strong=
      (
        energyRatio>=170 &&
        localDiffRatio>=118
      ) ||
      (
        localDiffRatio>=170 &&
        energyRatio>=118
      ) ||
      (
        energyRatio>=260 &&
        localDiffRatio>=108
      ) ||
      (
        localDiffRatio>=260 &&
        energyRatio>=108
      );

    bool active=
      (
        energyRatio>=130 &&
        localDiffRatio>=108
      ) ||
      (
        localDiffRatio>=140 &&
        energyRatio>=108
      ) ||
      (
        energyRatio>=200 &&
        localDiffRatio>=103
      ) ||
      (
        localDiffRatio>=200 &&
        energyRatio>=103
      );

    if(strong){
      strongFrames++;

      if(!speechStarted){
        speechStarted=true;
        firstSpeechFrame=f;
      }
    }

    if(!speechStarted) continue;

    if(active){
      activeFrames++;
      lastActiveFrame=f;
      silentRun=0;
    }else{
      silentRun++;
    }

    if(
      strongFrames>=3 &&
      activeFrames>=7 &&
      silentRun>=END_SILENCE_FRAMES
    ){
      break;
    }
  }

  if(
    !speechStarted ||
    strongFrames<3 ||
    activeFrames<7 ||
    lastActiveFrame<=firstSpeechFrame
  ){
    Serial.println(
      "[VOICE/VAD V2.4] Precise trim uncertain; using bounded best-effort speech window"
    );

    // We already reached this function only after live VAD confirmed speech.
    // Never throw away the whole user turn just because the offline trimmer
    // is uncertain. Use at most the first 10 seconds rather than all 15.
    startSample=0;

    windowSamples=samples;

    const size_t BEST_EFFORT_MAX=
      (size_t)STT_SAMPLE_RATE*
      10000UL/
      1000UL;

    if(
      windowSamples>
      BEST_EFFORT_MAX
    ){
      windowSamples=
        BEST_EFFORT_MAX;
    }

    Serial.print(
      "[VOICE/VAD V2.6] Best-effort samples: "
    );

    Serial.print(
      windowSamples
    );

    Serial.print(
      " (~"
    );

    Serial.print(
      (
        windowSamples*
        1000UL
      )/
      STT_SAMPLE_RATE
    );

    Serial.println(
      " ms)"
    );

    return
      windowSamples>0;
  }

  constexpr size_t PRE_ROLL_FRAMES=8;   // 160ms
  constexpr size_t POST_ROLL_FRAMES=8;  // 160ms

  size_t startFrame=
    firstSpeechFrame>PRE_ROLL_FRAMES
      ? firstSpeechFrame-PRE_ROLL_FRAMES
      : 0;

  size_t endFrame=lastActiveFrame+POST_ROLL_FRAMES+1;
  if(endFrame>frameCount) endFrame=frameCount;

  size_t selectedFrames=endFrame-startFrame;

  if(selectedFrames<18){
    Serial.println("[VOICE/VAD V2.4] Speech window too short");
    return false;
  }

  startSample=startFrame*STT_FRAME_SAMPLES;
  windowSamples=selectedFrames*STT_FRAME_SAMPLES;

  if(startSample+windowSamples>samples){
    windowSamples=samples-startSample;
  }

  Serial.print("[VOICE/VAD V2.6] Selected samples: ");
  Serial.print(windowSamples);
  Serial.print(" (~");
  Serial.print((windowSamples*1000UL)/STT_SAMPLE_RATE);
  Serial.println(" ms)");

  return windowSamples>0;
}

bool transcribeAutomaticFallback(String& command) {
  command = "";

  if (!groqSttConfigured()) {
    Serial.println("[VOICE/AUTO] STT fallback unavailable: Groq key missing");
    KIRA_SR.discardCapturedCommand();
    return false;
  }

  if (!kiraNetworkInternetAvailable()) {
    Serial.println("[VOICE/AUTO] STT fallback unavailable: no confirmed internet");
    KIRA_SR.discardCapturedCommand();
    return false;
  }

  const int16_t* captured = nullptr;
  size_t capturedSamples = 0;

  if (!KIRA_SR.capturedCommandView(captured, capturedSamples)) {
    Serial.println("[VOICE/AUTO] No preserved command audio");
    return false;
  }

  Serial.print("[VOICE/AUTO] Preserved samples: ");
  Serial.println(capturedSamples);

  if (KIRA_SR.captureOverflowed()) {
    Serial.println(
      "[VOICE/AUTO] WARNING: 15 s safety cap reached; V2.2 will trim before upload"
    );
  }

  size_t startSample = 0;
  size_t speechSamples = 0;

  if (!selectAdaptiveSpeechWindow(
        captured,
        capturedSamples,
        startSample,
        speechSamples
      )) {
    KIRA_SR.discardCapturedCommand();
    return false;
  }


  // Emergency-only latency guard.
  // A normal V2.6 turn should end long before the 15 s capture ceiling.
  // If the safety cap is nevertheless reached, do not send a full 480 KB
  // 15-second WAV to Whisper. Bound that emergency upload to 10 seconds.
  if(
    KIRA_SR.captureOverflowed()
  ){
    const size_t EMERGENCY_UPLOAD_MAX_SAMPLES=
      (size_t)STT_SAMPLE_RATE*
      10UL;

    if(
      speechSamples>
      EMERGENCY_UPLOAD_MAX_SAMPLES
    ){
      speechSamples=
        EMERGENCY_UPLOAD_MAX_SAMPLES;

      Serial.println(
        "[VOICE/VAD V2.6] Emergency upload bounded to 10 s"
      );
    }
  }


  bool ok = groqTranscribePcm(
    captured + startSample,
    speechSamples,
    command
  );

  KIRA_SR.discardCapturedCommand();

  if (!ok) {
    kiraRuntimePost(
      KIRA_EVENT_STT_FAILED,
      KIRA_EVENT_SOURCE_STT
    );

    return false;
  }

  kiraRuntimePost(
    KIRA_EVENT_STT_READY,
    KIRA_EVENT_SOURCE_STT
  );

  command.trim();

  if (!command.length()) {
    return false;
  }

  Serial.print("[VOICE/AUTO -> UNIVERSAL] ");
  Serial.println(command);

  return true;
}

void resumeWakeWordAfterSttFailure() {
  commandWatchdogArmed = false;
  voicePausedForCommand = false;
  currentVoiceCommand = -1;

  KIRA_SR.setMode(SR_MODE_WAKEWORD);
  KIRA_SR.resume();

  elliVisualSetState(ELLI_VISUAL_CONFUSED);

  Serial.println("[VOICE/STT] Free-form capture failed; WakeNet resumed");
  Serial.println("[VOICE] Waiting for wake word...");
}


bool serviceEarlyAutomaticFallbackWatchdog() {
  if (
    !commandWatchdogArmed ||
    voicePausedForCommand ||
    voiceQueue == nullptr
  ) {
    return false;
  }

  uint32_t now = millis();

  // Give MultiNet a short fair chance to recognize known local commands.
  if (
    now - commandListenStartMs <
    AUTO_STT_MIN_LISTEN_MS
  ) {
    return false;
  }

  // Normal path: V2.6 ends the turn about 1.1 s after the last convincing
  // HUMAN speech evidence. Steady fan/room energy cannot extend the turn.
  // Emergency path: the 15 s PSRAM cap is a REAL ceiling.
  bool safetyCap=
    KIRA_SR.captureOverflowed();

  bool speechEnded=
    KIRA_SR.commandCaptureSpeechEnded(
      AUTO_STT_END_SILENCE_MS
    );

  if(
    !speechEnded &&
    !safetyCap
  ){
    return false;
  }

  if(
    safetyCap &&
    !speechEnded
  ){
    Serial.println(
      "[VOICE/AUTO] HARD SAFETY CAP reached - forcing STT now"
    );
  }

  commandWatchdogArmed = false;
  srTimedOutWaitingForSpeechEnd = false;

  kiraRuntimePost(
    KIRA_EVENT_SPEECH_FINISHED,
    KIRA_EVENT_SOURCE_VAD
  );

  kiraRuntimePost(
    KIRA_EVENT_STT_REQUIRED,
    KIRA_EVENT_SOURCE_STT
  );

  Serial.print(
    safetyCap && !speechEnded
      ? "[VOICE/AUTO] Safety-cap handoff after "
      : "[VOICE/AUTO] Speech end detected after "
  );
  Serial.print(
    now - commandListenStartMs
  );
  Serial.println(
    " ms; starting automatic STT fallback"
  );

  // Freeze ESP-SR only AFTER the actual end-of-speech detector fires.
  KIRA_SR.pause();

  if (
    !KIRA_SR.preserveCurrentCommandCapture()
  ) {
    Serial.println(
      "[VOICE/AUTO] Speech-end preserve failed; keeping normal MultiNet path"
    );

    KIRA_SR.resume();
    commandWatchdogArmed = true;
    return false;
  }

  int fallbackId =
    VOICE_AUTO_STT_FALLBACK;

  voicePausedForCommand = true;
  currentVoiceCommand = fallbackId;

  elliVisualSetState(
    ELLI_VISUAL_THINKING
  );

  if (
    xQueueSend(
      voiceQueue,
      &fallbackId,
      0
    ) != pdTRUE
  ) {
    Serial.println(
      "[VOICE/AUTO] Speech-end fallback queue unavailable"
    );

    KIRA_SR.discardCapturedCommand();
    voicePausedForCommand = false;
    currentVoiceCommand = -1;

    KIRA_SR.setMode(
      SR_MODE_WAKEWORD
    );
    KIRA_SR.resume();

    elliVisualSetState(
      ELLI_VISUAL_IDLE
    );

    return false;
  }

  return true;
}

bool captureAndTranscribeFreeform(String& command) {
  command = "";

  if (!groqSttConfigured()) {
    Serial.println("[VOICE/STT] Free-form STT unavailable: Groq key missing");
    return false;
  }

  if (!kiraNetworkInternetAvailable()) {
    Serial.println("[VOICE/STT] Free-form STT unavailable: no confirmed internet");
    return false;
  }

  // Prompt: speaker is enabled only for a very short cue, then muted
  // before microphone capture begins.
  digitalWrite(AMP_SD, HIGH);
  delay(30);
  playToneHz(900.0f, 75);
  sendSpeakerSilence();
  delay(20);
  digitalWrite(AMP_SD, LOW);

  elliVisualSetState(ELLI_VISUAL_LISTENING);
  delay(120);

  int16_t* pcm = nullptr;
  size_t samples = 0;

  if (!captureSttPcm(pcm, samples)) {
    return false;
  }

  bool ok = groqTranscribePcm(
    pcm,
    samples,
    command
  );

  heap_caps_free(pcm);
  pcm = nullptr;

  return ok;
}

// ============================================================
// ESP-SR CALLBACK
// ============================================================

void onSpeechEvent(
  sr_event_t event,
  int commandId,
  int phraseId
) {

  switch(event) {

    case SR_EVENT_WAKEWORD:
    case SR_EVENT_WAKEWORD_CHANNEL:
    {
      if(
        !kiraWakeAcceptDetection(
          event
        )
      ) {
        KIRA_SR.setMode(
          SR_MODE_WAKEWORD
        );

        break;
      }

      Serial.println();
      Serial.println(
        "[VOICE] WakeNet wake word accepted by Wake Service"
      );

      elliVisualSetState(
        ELLI_VISUAL_LISTENING
      );

      // Start the independent 15 s AudioInput utterance collector.
      kiraAudioEngineStartUtterance();

      commandListenStartMs =
        millis();

      commandWatchdogArmed =
        true;

      srTimedOutWaitingForSpeechEnd =
        false;

      KIRA_SR.setMode(
        SR_MODE_COMMAND
      );

      break;
    }


    case SR_EVENT_COMMAND:
    {

      commandWatchdogArmed = false;
      srTimedOutWaitingForSpeechEnd = false;

      kiraRuntimePost(
        KIRA_EVENT_SPEECH_FINISHED,
        KIRA_EVENT_SOURCE_AUDIO
      );

      kiraRuntimePost(
        KIRA_EVENT_OFFLINE_COMMAND_MATCHED,
        KIRA_EVENT_SOURCE_ROUTER,
        commandId
      );

      Serial.print(
        "[VOICE] Offline command ID: "
      );

      Serial.println(
        commandId
      );

      if(
        phraseId >= 0 &&
        (size_t)phraseId < VOICE_PHRASE_COUNT
      ) {

        Serial.print(
          "[VOICE] Matched phrase: "
        );

        Serial.println(
          voiceCommands[phraseId].str
        );
      }

      KIRA_SR.pause();

      voicePausedForCommand =
        true;

      elliVisualSetState(
        ELLI_VISUAL_THINKING
      );

      if(
        voiceQueue !=
        nullptr
      ) {

        if(
          xQueueSend(
            voiceQueue,
            &commandId,
            0
          ) !=
          pdTRUE
        ) {

          Serial.println(
            "[VOICE] Command queue full"
          );

          voicePausedForCommand =
            false;

          KIRA_SR.setMode(
            SR_MODE_WAKEWORD
          );

          KIRA_SR.resume();

          elliVisualSetState(
            ELLI_VISUAL_IDLE
          );
        }
      }

      break;
    }


    case SR_EVENT_TIMEOUT:
    {
      // MultiNet timing is no longer the user's recording limit.
      // AudioInput continues capturing independently.

#if KIRA_AUDIO_ENGINE_ENABLED && KIRA_VAD_V2_ENABLED
      if(
        !KIRA_SR.commandCaptureSpeechSeen()
      ) {
        commandWatchdogArmed =
          false;

        srTimedOutWaitingForSpeechEnd =
          false;

        Serial.print(
          "[VAD V2] FALSE_TRIGGER_REJECT noise="
        );

        Serial.print(
          kiraVadNoiseLevel()
        );

        Serial.print(
          " level="
        );

        Serial.println(
          kiraVadLastLevel()
        );

        Serial.println(
          "[VOICE/AUTO] No confirmed human speech - NO STT / NO AI request"
        );

        kiraMetricsIncrement(
          KIRA_METRIC_VAD_FALSE_TRIGGERS
        );

        kiraRuntimePost(
          KIRA_EVENT_SPEECH_REJECTED,
          KIRA_EVENT_SOURCE_VAD
        );

        KIRA_SR.discardCapturedCommand();

        KIRA_SR.setMode(
          SR_MODE_WAKEWORD
        );

        KIRA_SR.resume();

        voicePausedForCommand =
          false;

        currentVoiceCommand =
          -1;

        elliVisualSetState(
          ELLI_VISUAL_IDLE
        );

        Serial.println(
          "[VOICE] Waiting for wake word..."
        );

        break;
      }
#endif

      // If the human is still talking, DO NOT freeze their audio.
      if(
        !KIRA_SR.commandCaptureSpeechEnded(
          AUTO_STT_END_SILENCE_MS
        )
      ) {
        srTimedOutWaitingForSpeechEnd =
          true;

        commandWatchdogArmed =
          true;

        Serial.println(
          "[VOICE/AUTO] MultiNet timeout while speech still active"
        );

        Serial.println(
          "[VOICE/AUTO] Direct ESP-SR mic feed keeps preserved capture running until real speech end"
        );

        break;
      }

      srTimedOutWaitingForSpeechEnd =
        false;

      commandWatchdogArmed =
        true;

      Serial.println(
        "[VOICE/AUTO] MultiNet timeout after speech end; handing preserved utterance to STT"
      );

      // Reuse the same speech-end watchdog path from the normal loop.
      break;
    }


    default:
      break;
  }
}


// ============================================================
// AUDIO STARTUP
// ============================================================

bool beginSpeaker() {

  digitalWrite(
    AMP_SD,
    LOW
  );

  pinMode(
    AMP_SD,
    OUTPUT
  );

  digitalWrite(
    AMP_SD,
    LOW
  );

  SpeakerI2S.setPins(
    AMP_BCLK,
    AMP_LRC,
    AMP_DIN
  );

  return SpeakerI2S.begin(
    I2S_MODE_STD,
    SPK_SAMPLE_RATE,
    I2S_DATA_BIT_WIDTH_16BIT,
    I2S_SLOT_MODE_STEREO
  );
}


bool beginMicrophone() {

  MicI2S.setTimeout(
    1000
  );

  MicI2S.setPins(
    MIC_BCLK,
    MIC_WS,
    -1,
    MIC_DIN
  );

  bool ok =
    MicI2S.begin(
      I2S_MODE_STD,
      MIC_SAMPLE_RATE,
      I2S_DATA_BIT_WIDTH_32BIT,
      I2S_SLOT_MODE_MONO,
      I2S_STD_SLOT_LEFT
    );

  if(
    !ok
  ) {

    return false;
  }

  return MicI2S.configureRX(
    MIC_SAMPLE_RATE,
    I2S_DATA_BIT_WIDTH_32BIT,
    I2S_SLOT_MODE_MONO,
    I2S_RX_TRANSFORM_32_TO_16,
    I2S_STD_SLOT_LEFT
  );
}


} // namespace



// ============================================================
// SHARED GROQ CREDENTIAL BRIDGE
// ============================================================
//
// TTS obtains the same Groq credential already used by the
// working STT path.  The key itself is never printed.
// ============================================================

const char* kiraVoiceGroqApiKey() {
  return KIRA_GROQ_API_KEY;
}

// ============================================================
// PHASE 5L - TTS SPEAKER BRIDGE
// ============================================================

bool kiraVoiceTtsSpeakerConfigure(
  uint32_t sampleRate
) {
  if(
    sampleRate < 8000 ||
    sampleRate > 48000
  ) {
    return false;
  }

  // ==========================================================
  // PHASE 5L.2D - CLEAN I2S RESTART FOR TTS
  // ==========================================================
  //
  // Do not reconfigure a live TX/DMA stream. Fully stop the
  // speaker I2S channel, then start a fresh channel at the WAV
  // sample rate. This removes stale clock/DMA state that can
  // sound like crackles or tiny dropouts.
  // ==========================================================

  digitalWrite(
    AMP_SD,
    LOW
  );

  delay(10);

  SpeakerI2S.end();

  delay(10);

  SpeakerI2S.setPins(
    AMP_BCLK,
    AMP_LRC,
    AMP_DIN
  );

  bool ok =
    SpeakerI2S.begin(
      I2S_MODE_STD,
      sampleRate,
      I2S_DATA_BIT_WIDTH_16BIT,
      I2S_SLOT_MODE_STEREO
    );

  if(
    ok
  ) {
    Serial.print(
      "[VOICE/TTS] Fresh I2S TX started at "
    );
    Serial.print(
      sampleRate
    );
    Serial.println(
      " Hz"
    );
  }
  else {
    Serial.println(
      "[VOICE/TTS] Fresh I2S TX start FAILED"
    );
  }

  return ok;
}


size_t kiraVoiceTtsSpeakerWrite(
  const uint8_t* data,
  size_t bytes
) {
  if(
    data == nullptr ||
    bytes == 0
  ) {
    return 0;
  }

  return SpeakerI2S.write(
    data,
    bytes
  );
}


void kiraVoiceTtsSpeakerEnable(
  bool enabled
) {
  digitalWrite(
    AMP_SD,
    enabled ? HIGH : LOW
  );
}


void kiraVoiceTtsSpeakerSilence() {
  sendSpeakerSilence();
}


void kiraVoiceTtsSpeakerRestore() {

  digitalWrite(
    AMP_SD,
    LOW
  );

  delay(10);

  SpeakerI2S.end();

  delay(10);

  SpeakerI2S.setPins(
    AMP_BCLK,
    AMP_LRC,
    AMP_DIN
  );

  bool ok =
    SpeakerI2S.begin(
      I2S_MODE_STD,
      SPK_SAMPLE_RATE,
      I2S_DATA_BIT_WIDTH_16BIT,
      I2S_SLOT_MODE_STEREO
    );

  if(
    ok
  ) {
    sendSpeakerSilence();

    Serial.println(
      "[VOICE/TTS] Speaker I2S restored cleanly"
    );
  }
  else {
    Serial.println(
      "[VOICE/TTS] Speaker I2S restore FAILED"
    );
  }
}


// ============================================================
// PUBLIC BEGIN
// ============================================================

bool kiraVoiceBegin() {

  if(
    voiceReadyFlag
  ) {

    return true;
  }


  Serial.println();
  Serial.println(
    "======================================"
  );
  Serial.println(
    " KIRA OFFLINE VOICE - PHASE 5"
  );
  Serial.println(
    "======================================"
  );


  voiceQueue =
    xQueueCreate(
      6,
      sizeof(int)
    );

  if(
    voiceQueue ==
    nullptr
  ) {

    Serial.println(
      "[VOICE] ERROR: queue creation failed"
    );

    return false;
  }


  Serial.println(
    "[VOICE] Starting MAX98357A..."
  );

  if(
    !beginSpeaker()
  ) {

    Serial.println(
      "[VOICE] ERROR: speaker I2S failed"
    );

    return false;
  }

  Serial.println(
    "[VOICE] Speaker READY"
  );


  Serial.println(
    "[VOICE] Starting INMP441..."
  );

  if(
    !beginMicrophone()
  ) {

    Serial.println(
      "[VOICE] ERROR: microphone I2S failed"
    );

    digitalWrite(
      AMP_SD,
      LOW
    );

    return false;
  }

  Serial.println(
    "[VOICE] Microphone READY"
  );

#if KIRA_WAKE_SERVICE_ENABLED
  kiraWakeBegin();
#endif


  KIRA_SR.onEvent(
    onSpeechEvent
  );


  Serial.print(
    "[VOICE] Offline phrases loaded: "
  );

  Serial.print(
    VOICE_PHRASE_COUNT
  );

  Serial.println(
    " / 200"
  );


  Serial.println(
    "[VOICE] Loading ESP-SR models..."
  );

#if KIRA_AUDIO_ENGINE_ENABLED
  kiraAudioEngineBegin();
#endif

  bool srOK =
    KIRA_SR.begin(
      MicI2S,
      voiceCommands,
      sizeof(voiceCommands) /
        sizeof(voiceCommands[0]),
      SR_CHANNELS_MONO,
      SR_MODE_WAKEWORD,
      "M"
    );


  if(
    !srOK
  ) {

    Serial.println(
      "[VOICE] ERROR: ESP-SR failed to start"
    );

    Serial.println(
      "[VOICE] KIRA continues without offline voice."
    );

    digitalWrite(
      AMP_SD,
      LOW
    );

    return false;
  }


  voiceReadyFlag =
    true;

  voicePausedForCommand =
    false;

  currentVoiceCommand =
    -1;


  Serial.println();
  Serial.println(
    "[VOICE] ESP-SR READY"
  );

  Serial.println(
    "[VOICE] Stock true WakeNet word: Hi ESP"
  );

  Serial.println(
    "[VOICE] Text/STT Elli aliases: Hey Elli / Hi Elli / Hello Elli / Okay Elli / Wake up Elli"
  );

  Serial.println(
    "[VOICE] Elli aliases require a custom WakeNet model to become true always-listening wake words."
  );

  Serial.print(
    "[VOICE/STT] Phase 5J-2 automatic fallback: "
  );

  Serial.println(
    groqSttConfigured()
      ? "READY - unknown post-wake speech can route to STT"
      : "DISABLED - Groq key not configured"
  );

  Serial.println(
    "[VOICE/STT] Legacy Ask Elli trigger remains available for compatibility"
  );

  Serial.println(
    "[VOICE/VAD] Group1 V2.6 endpoint = 1100 ms | two-stage fan-safe | hard cap = 15 s"
  );

  Serial.println(
    "[VOICE/SAFE] Auto conversation sessions: DISABLED"
  );

  Serial.println(
    "[VOICE/SAFE] One wake -> one command -> one answer -> WakeNet"
  );

  Serial.println(
    "[VOICE/SAFE] Fan/noise STT token guard: ENABLED"
  );

  Serial.print(
    "[VOICE] Offline command phrases loaded: "
  );

  Serial.println(
    sizeof(voiceCommands) /
      sizeof(voiceCommands[0])
  );

  Serial.println(
    "[VOICE] Waiting for wake word..."
  );


  return true;
}


// ============================================================
// READY
// ============================================================

bool kiraVoiceReady() {

  return
    voiceReadyFlag;
}


// ============================================================
// QUEUE -> UNIVERSAL BRAIN
// ============================================================

bool kiraVoiceTakeCommand(
  String& command
) {

  command = "";

  if(
    !voiceReadyFlag ||
    voiceQueue ==
      nullptr
  ) {

    return false;
  }


  // Service the early-fallback deadline from the normal KIRA loop,
  // never from the ESP-SR callback/task itself.
  serviceEarlyAutomaticFallbackWatchdog();


  int commandId =
    -1;


  if(
    xQueueReceive(
      voiceQueue,
      &commandId,
      0
    ) !=
    pdTRUE
  ) {

    return false;
  }


  // ==========================================================
  // PHASE 5J-2 AUTOMATIC OFFLINE-vs-STT FALLBACK
  // ==========================================================
  //
  // A known MultiNet phrase never reaches this branch; it was already
  // converted locally. This sentinel only arrives after MultiNet timed
  // out while KIRA_SR preserved the user's original command audio.
  // ==========================================================

  if (
    commandId ==
    VOICE_AUTO_STT_FALLBACK
  ) {
    currentVoiceCommand = commandId;

    if (
      transcribeAutomaticFallback(
        command
      )
    ) {
      return true;
    }

    command = "";
    resumeWakeWordAfterSttFailure();
    return false;
  }


  // ==========================================================
  // PHASE 5J FREE-FORM STT GATE
  // ==========================================================
  //
  // "Ask Elli" is itself an offline MultiNet command. Once that
  // command is recognized, ESP-SR is already paused by the callback.
  // We temporarily capture raw microphone PCM, transcribe it online,
  // and return the transcript as an ordinary Universal Brain command.
  // ==========================================================

  if(
    commandId ==
    VOICE_FREEFORM_STT
  ) {

    currentVoiceCommand =
      commandId;

    if(
      captureAndTranscribeFreeform(
        command
      )
    ) {

      Serial.print(
        "[VOICE/STT -> UNIVERSAL] "
      );

      Serial.println(
        command
      );

      return true;
    }

    command =
      "";

    resumeWakeWordAfterSttFailure();

    return false;
  }


  if(
    !voiceCommandToText(
      commandId,
      command
    )
  ) {

    Serial.println(
      "[VOICE] Unknown queued command"
    );

    currentVoiceCommand =
      commandId;

    kiraVoiceCommandFinished(false);

    command = "";

    return false;
  }


  currentVoiceCommand =
    commandId;


  Serial.print(
    "[VOICE -> UNIVERSAL] "
  );

  Serial.println(
    command
  );


  return true;
}



// ============================================================
// GLOBAL TTS RECOGNITION GUARD
// ============================================================

bool kiraVoicePauseForTts() {

  if(
    !voiceReadyFlag
  ) {
    return false;
  }


  if(
    voicePausedForCommand
  ) {
    return false;
  }


  bool paused =
    KIRA_SR.pause();


  if(paused) {

    delay(20);

    Serial.println(
      "[VOICE/TTS] WakeNet paused for Elli speech"
    );
  }


  return paused;
}


void kiraVoiceResumeAfterTts(
  bool pausedByTts
) {

  if(
    !pausedByTts ||
    !voiceReadyFlag
  ) {
    return;
  }


  // SAFE TOKEN GUARD V1:
  // When TTS belongs to the current voice command, keep ESP-SR paused.
  // kiraVoiceCommandFinished() is the ONLY place allowed to resume
  // recognition after the reply. This prevents double-resume/self-hearing.
  if(
    voicePausedForCommand
  ) {

    Serial.println(
      "[VOICE/TTS] Command active - recognition remains paused"
    );

    return;
  }


  KIRA_SR.setMode(
    SR_MODE_WAKEWORD
  );

  KIRA_SR.resume();


  Serial.println(
    "[VOICE/TTS] WakeNet resumed after background speech"
  );
}


// ============================================================
// UNIVERSAL COMMAND FINISHED
// ============================================================

void kiraVoiceCommandFinished(
  bool speechPlayed
) {

  if(
    !voiceReadyFlag ||
    !voicePausedForCommand
  ) {

    return;
  }


  Serial.println(
    "[VOICE] Universal command complete"
  );


  // Global TTS already spoke every elliSay() response.
  // Keep the old tone only when no speech actually played.

  if(
    !speechPlayed
  ) {

    Serial.println(
      "[VOICE/TTS] No spoken Elli reply - using beep fallback"
    );

    playAcknowledgement(
      currentVoiceCommand
    );
  }


  // Let speaker/reverb tail die before WakeNet is armed again.
  delay(
    speechPlayed ? 700 : 120
  );


  kiraWakeArmIdle();

  KIRA_SR.setMode(
    SR_MODE_WAKEWORD
  );

  KIRA_SR.resume();


  if(
    !speechPlayed
  ) {
    kiraRuntimeRequestState(
      KIRA_STATE_IDLE,
      "VOICE_COMMAND_FINISHED"
    );
  }


  voicePausedForCommand =
    false;

  currentVoiceCommand =
    -1;


  Serial.println(
    "[VOICE] Recognition resumed"
  );

  Serial.println(
    "[VOICE] Waiting for wake word..."
  );
}
