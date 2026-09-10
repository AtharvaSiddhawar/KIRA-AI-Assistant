#include "kira_audio_engine.h"

#include "kira_audio_codec.h"
#include "kira_audio_output.h"
#include "kira_audio_stream.h"
#include "kira_next_config.h"
#include "kira_vad.h"

namespace {
volatile uint32_t inputFrames = 0;
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
bool ready = false;
}

bool kiraAudioEngineBegin() {
  if (ready) return true;

#if KIRA_AUDIO_ENGINE_ENABLED
  kiraVadBegin();
  kiraAudioCodecBegin();

  // Group B uplink is only started when the complete remote streaming path
  // is actually enabled. This keeps the current direct-provider KIRA build
  // link-safe while WebSocket/Gateway/Opus remain OFF.
#if KIRA_AUDIO_UPLINK_ENABLED && KIRA_GATEWAY_ENABLED && KIRA_OPUS_ENABLED
  if (!kiraAudioStreamBegin()) {
    Serial.println("[AUDIO CORE] WARNING: microphone uplink queue unavailable");
  }
#endif

#if KIRA_STREAMING_TTS_ENABLED
  if (!kiraAudioOutputBegin()) {
    Serial.println("[AUDIO CORE] WARNING: queued audio output unavailable");
  }
#endif

  ready = true;
  Serial.println("[AUDIO CORE] READY | input=16kHz mono | frame=20ms/320 samples");
  Serial.println("[AUDIO CORE] GROUP B LINKFIX V1 | direct-provider safe");
#else
  // Keep the public API linkable even when the engine feature is disabled.
  ready = true;
#endif

  return true;
}

void kiraAudioEngineObserveInput(const int16_t* samples, size_t count) {
  if (!samples || count == 0) return;

#if KIRA_AUDIO_ENGINE_ENABLED
  if (!ready) kiraAudioEngineBegin();

  kiraVadProcessBlock(samples, count);

#if KIRA_AUDIO_UPLINK_ENABLED && KIRA_GATEWAY_ENABLED && KIRA_OPUS_ENABLED
  // Producer only: the streaming module performs no network I/O here.
  kiraAudioStreamObserveInput(samples, count);
#endif

  const uint32_t frames =
    (uint32_t)((count + KIRA_AUDIO_FRAME_SAMPLES - 1) / KIRA_AUDIO_FRAME_SAMPLES);

  portENTER_CRITICAL(&audioMux);
  inputFrames += frames;
  portEXIT_CRITICAL(&audioMux);
#else
  (void)samples;
  (void)count;
#endif
}

void kiraAudioEngineStartUtterance() {
#if KIRA_AUDIO_ENGINE_ENABLED
  if (!ready) kiraAudioEngineBegin();
  kiraVadStartUtterance();

#if KIRA_AUDIO_UPLINK_ENABLED && KIRA_GATEWAY_ENABLED && KIRA_OPUS_ENABLED
  kiraAudioStreamStartTurn();
#endif
#endif
}

void kiraAudioEngineCancelUtterance() {
#if KIRA_AUDIO_ENGINE_ENABLED
#if KIRA_AUDIO_UPLINK_ENABLED && KIRA_GATEWAY_ENABLED && KIRA_OPUS_ENABLED
  kiraAudioStreamCancelTurn();
#endif
  kiraVadCancelUtterance();
#endif
}

bool kiraAudioEngineSpeechSeen() {
#if KIRA_AUDIO_ENGINE_ENABLED
  return kiraVadSpeechSeen();
#else
  return false;
#endif
}

bool kiraAudioEngineSpeechEnded(uint32_t silenceMs) {
#if KIRA_AUDIO_ENGINE_ENABLED
  return kiraVadSpeechEnded(silenceMs);
#else
  (void)silenceMs;
  return false;
#endif
}

uint32_t kiraAudioEngineInputFrames() {
  uint32_t value;
  portENTER_CRITICAL(&audioMux);
  value = inputFrames;
  portEXIT_CRITICAL(&audioMux);
  return value;
}
