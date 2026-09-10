#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

#include "kira_tts_hybrid.h"
#include "kira_tts_config.h"
#include "elli_visual.h"

// Speaker bridge implemented by kira_voice.cpp.
bool kiraVoiceTtsSpeakerConfigure(uint32_t sampleRate);
size_t kiraVoiceTtsSpeakerWrite(const uint8_t* data, size_t bytes);
void kiraVoiceTtsSpeakerEnable(bool enabled);
void kiraVoiceTtsSpeakerSilence();
void kiraVoiceTtsSpeakerRestore();

#if KIRA_PICOTTS_ENABLED && __has_include("picotts.h")
extern "C" {
  #include "picotts.h"
}
#define KIRA_PICOTTS_AVAILABLE 1
#else
#define KIRA_PICOTTS_AVAILABLE 0
#endif

namespace {

constexpr uint32_t EDGE_PCM_RATE = 24000;
constexpr uint32_t PICO_PCM_RATE = 16000;
constexpr uint32_t HYBRID_HTTP_TIMEOUT_MS = 30000;

// Keep backup speech comfortably below digital full scale.
constexpr int32_t HYBRID_GAIN_NUM = 3;
constexpr int32_t HYBRID_GAIN_DEN = 5;

uint32_t mouthEnvelope = 0;


String cleanSpeechText(String text) {
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.replace("**", "");
  text.replace("__", "");
  text.replace("`", "");
  text.replace("#", "");

  while(text.indexOf("  ") >= 0) {
    text.replace("  ", " ");
  }

  text.trim();
  return text;
}


String jsonEscapeHybrid(const String& input) {
  String out;
  out.reserve(input.length() + 24);

  for(size_t i = 0; i < input.length(); i++) {
    char c = input[i];

    switch(c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += ' '; break;
      default:
        if((uint8_t)c >= 0x20) out += c;
        break;
    }
  }

  return out;
}


int16_t scaleHybridPcm(int16_t sample) {
  int32_t v =
    ((int32_t)sample * HYBRID_GAIN_NUM) /
    HYBRID_GAIN_DEN;

  if(v > 32767) v = 32767;
  if(v < -32768) v = -32768;
  return (int16_t)v;
}


uint8_t mouthLevelHybrid(
  const int16_t* samples,
  size_t count
) {
  if(!samples || count == 0) return 0;

  uint64_t sum = 0;
  uint32_t peak = 0;
  size_t used = 0;

  for(size_t i = 0; i < count; i += 2) {
    int32_t v = samples[i];
    if(v < 0) v = -v;
    if(v > 32767) v = 32767;

    sum += (uint32_t)v;
    if((uint32_t)v > peak) peak = (uint32_t)v;
    used++;
  }

  if(!used) return 0;

  uint32_t mean = (uint32_t)(sum / used);
  uint32_t activity = mean + peak / 10;

  if(activity > mouthEnvelope) {
    mouthEnvelope =
      (mouthEnvelope + activity * 3UL) / 4UL;
  }
  else {
    mouthEnvelope =
      (mouthEnvelope * 5UL + activity) / 6UL;
  }

  if(mouthEnvelope < 420)  return 0;
  if(mouthEnvelope < 1800) return 1;
  if(mouthEnvelope < 4800) return 2;
  return 3;
}


bool speakerWriteAll(
  const uint8_t* data,
  size_t bytes
) {
  size_t sent = 0;
  uint32_t lastProgress = millis();

  while(sent < bytes) {
    size_t n =
      kiraVoiceTtsSpeakerWrite(
        data + sent,
        bytes - sent
      );

    if(n > 0) {
      sent += n;
      lastProgress = millis();
      continue;
    }

    if(millis() - lastProgress > 1500) {
      return false;
    }

    delay(1);
  }

  return true;
}


bool playMonoPcm16(
  const int16_t* pcm,
  size_t samples,
  uint32_t sampleRate
) {
  if(!pcm || !samples) return false;

  if(!kiraVoiceTtsSpeakerConfigure(sampleRate)) {
    Serial.println("[TTS/HYBRID] Speaker configure failed");
    return false;
  }

  kiraVoiceTtsSpeakerEnable(true);
  delay(35);

  mouthEnvelope = 0;
  elliVisualAudioSpeechBegin();

  static int16_t stereo[2048];
  size_t offset = 0;
  bool ok = true;

  while(offset < samples) {
    size_t block = samples - offset;
    if(block > 1024) block = 1024;

    const int16_t* input = pcm + offset;

    elliVisualAudioSpeechLevel(
      mouthLevelHybrid(input, block)
    );

    for(size_t i = 0; i < block; i++) {
      int16_t s = scaleHybridPcm(input[i]);
      stereo[i * 2] = s;
      stereo[i * 2 + 1] = s;
    }

    size_t outBytes =
      block * 2 * sizeof(int16_t);

    if(
      !speakerWriteAll(
        reinterpret_cast<const uint8_t*>(stereo),
        outBytes
      )
    ) {
      ok = false;
      break;
    }

    offset += block;
  }

  elliVisualAudioSpeechLevel(0);
  kiraVoiceTtsSpeakerSilence();
  delay(25);
  kiraVoiceTtsSpeakerEnable(false);
  kiraVoiceTtsSpeakerRestore();
  elliVisualAudioSpeechEnd();

  return ok;
}


bool edgeConfigured() {
#if KIRA_EDGE_TTS_ENABLED
  return strlen(KIRA_EDGE_TTS_URL) > 12;
#else
  return false;
#endif
}


bool downloadHttpBodyToPsram(
  HTTPClient& http,
  uint8_t*& data,
  size_t& bytes
) {
  data = nullptr;
  bytes = 0;

  NetworkClient* stream = http.getStreamPtr();
  if(!stream) return false;

  stream->setTimeout(8000);

  size_t freePsram = ESP.getFreePsram();
  const size_t reserve = 700UL * 1024UL;

  if(freePsram <= reserve + 32768UL) {
    return false;
  }

  size_t safeMax = freePsram - reserve;
  if(safeMax > 4UL * 1024UL * 1024UL) {
    safeMax = 4UL * 1024UL * 1024UL;
  }

  int announced = http.getSize();

  if(announced > 0) {
    size_t wanted = (size_t)announced;
    if(wanted > safeMax) return false;

    uint8_t* buffer =
      static_cast<uint8_t*>(
        heap_caps_malloc(
          wanted,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        )
      );

    if(!buffer) return false;

    size_t got = 0;
    uint32_t lastData = millis();

    while(got < wanted) {
      int available = stream->available();

      if(available > 0) {
        size_t n = wanted - got;
        if(n > (size_t)available) n = (size_t)available;

        size_t r = stream->readBytes(
          reinterpret_cast<char*>(buffer + got),
          n
        );

        if(r > 0) {
          got += r;
          lastData = millis();
          continue;
        }
      }

      if(millis() - lastData > 15000) {
        heap_caps_free(buffer);
        return false;
      }

      delay(1);
    }

    data = buffer;
    bytes = got;
    return true;
  }

  size_t capacity = 96UL * 1024UL;
  if(capacity > safeMax) capacity = safeMax;

  uint8_t* buffer =
    static_cast<uint8_t*>(
      heap_caps_malloc(
        capacity,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
      )
    );

  if(!buffer) return false;

  size_t got = 0;
  uint32_t lastData = millis();

  while(true) {
    int available = stream->available();

    if(available > 0) {
      size_t required = got + (size_t)available;

      if(required > capacity) {
        size_t next = capacity;
        while(next < required && next < safeMax) {
          size_t doubled = next * 2;
          if(doubled <= next) break;
          next = doubled;
          if(next > safeMax) next = safeMax;
        }

        if(next < required) {
          heap_caps_free(buffer);
          return false;
        }

        uint8_t* grown =
          static_cast<uint8_t*>(
            heap_caps_realloc(
              buffer,
              next,
              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            )
          );

        if(!grown) {
          heap_caps_free(buffer);
          return false;
        }

        buffer = grown;
        capacity = next;
      }

      size_t r = stream->readBytes(
        reinterpret_cast<char*>(buffer + got),
        (size_t)available
      );

      if(r > 0) {
        got += r;
        lastData = millis();
        continue;
      }
    }

    if(!stream->connected() && stream->available() <= 0) {
      break;
    }

    if(got > 0 && millis() - lastData > 1500) {
      break;
    }

    if(got == 0 && millis() - lastData > HYBRID_HTTP_TIMEOUT_MS) {
      heap_caps_free(buffer);
      return false;
    }

    delay(1);
  }

  if(!got) {
    heap_caps_free(buffer);
    return false;
  }

  data = buffer;
  bytes = got;
  return true;
}


bool speakEdge(const String& inputText) {
#if !KIRA_EDGE_TTS_ENABLED
  (void)inputText;
  return false;
#else
  if(!edgeConfigured()) return false;
  if(WiFi.status() != WL_CONNECTED) return false;

  String text = cleanSpeechText(inputText);
  if(!text.length()) return false;
  if(text.length() > 3500) text.remove(3500);

  String payload;
  payload.reserve(text.length() + 220);
  payload += "{\"model\":\"tts-1\",\"input\":\"";
  payload += jsonEscapeHybrid(text);
  payload += "\",\"voice\":\"";
  payload += KIRA_EDGE_TTS_VOICE;
  payload += "\",\"response_format\":\"pcm\",\"speed\":1.0}";

  HTTPClient http;
  http.setTimeout(HYBRID_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  WiFiClient plain;
  WiFiClientSecure secure;
  secure.setInsecure();

  String url = KIRA_EDGE_TTS_URL;
  bool began = false;

  if(url.startsWith("https://")) {
    began = http.begin(secure, url);
  }
  else {
    began = http.begin(plain, url);
  }

  if(!began) {
    Serial.println("[TTS/EDGE] HTTP begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/octet-stream");

  if(strlen(KIRA_EDGE_TTS_API_KEY) > 0) {
    http.addHeader(
      "Authorization",
      "Bearer " + String(KIRA_EDGE_TTS_API_KEY)
    );
  }

  Serial.println("[TTS/HYBRID] Trying Edge Emma backup");

  int code = http.POST(
    reinterpret_cast<uint8_t*>(
      const_cast<char*>(payload.c_str())
    ),
    payload.length()
  );

  Serial.print("[TTS/EDGE] HTTP ");
  Serial.println(code);

  if(code < 200 || code >= 300) {
    if(code > 0) {
      String error = http.getString();
      error.trim();
      if(error.length() > 180) error.remove(180);
      if(error.length()) {
        Serial.print("[TTS/EDGE] Server response: ");
        Serial.println(error);
      }
    }

    http.end();
    return false;
  }

  uint8_t* raw = nullptr;
  size_t rawBytes = 0;

  bool downloaded =
    downloadHttpBodyToPsram(
      http,
      raw,
      rawBytes
    );

  http.end();

  if(!downloaded) {
    Serial.println("[TTS/EDGE] PCM download failed");
    return false;
  }

  rawBytes &= ~((size_t)1);

  Serial.print("[TTS/EDGE] PCM bytes: ");
  Serial.println(rawBytes);

  bool played = playMonoPcm16(
    reinterpret_cast<const int16_t*>(raw),
    rawBytes / sizeof(int16_t),
    EDGE_PCM_RATE
  );

  heap_caps_free(raw);

  if(played) {
    Serial.println("[TTS/HYBRID] Edge Emma succeeded");
  }

  return played;
#endif
}


#if KIRA_PICOTTS_AVAILABLE

volatile bool picoDone = false;
volatile bool picoError = false;
volatile bool picoWriteError = false;

void picoSamples(
  int16_t* buffer,
  unsigned count
) {
  if(!buffer || !count || picoWriteError) return;

  static int16_t stereo[1024];
  size_t offset = 0;

  while(offset < count) {
    size_t block = count - offset;
    if(block > 512) block = 512;

    int16_t* input = buffer + offset;

    elliVisualAudioSpeechLevel(
      mouthLevelHybrid(input, block)
    );

    for(size_t i = 0; i < block; i++) {
      int16_t s = scaleHybridPcm(input[i]);
      stereo[i * 2] = s;
      stereo[i * 2 + 1] = s;
    }

    size_t bytes = block * 2 * sizeof(int16_t);

    if(
      !speakerWriteAll(
        reinterpret_cast<const uint8_t*>(stereo),
        bytes
      )
    ) {
      picoWriteError = true;
      return;
    }

    offset += block;
  }
}

void picoIdle() {
  picoDone = true;
}

void picoFailure() {
  picoError = true;
  picoDone = true;
}

#endif


bool picoConfigured() {
#if KIRA_PICOTTS_AVAILABLE
  return true;
#else
  return false;
#endif
}


bool speakPico(const String& inputText) {
#if !KIRA_PICOTTS_AVAILABLE
  (void)inputText;

  #if KIRA_PICOTTS_ENABLED
  Serial.println(
    "[TTS/PICO] Enabled, but picotts.h/component is unavailable"
  );
  #endif

  return false;
#else
  String text = cleanSpeechText(inputText);
  if(!text.length()) return false;
  if(text.length() > 1800) text.remove(1800);

  Serial.println(
    "[TTS/HYBRID] Trying PicoTTS true-offline backup"
  );

  picoDone = false;
  picoError = false;
  picoWriteError = false;

  if(
    !picotts_init(
      KIRA_PICOTTS_PRIORITY,
      picoSamples,
      KIRA_PICOTTS_CORE
    )
  ) {
    Serial.println("[TTS/PICO] picotts_init failed");
    return false;
  }

  picotts_set_idle_notify(picoIdle);
  picotts_set_error_notify(picoFailure);

  if(!kiraVoiceTtsSpeakerConfigure(PICO_PCM_RATE)) {
    Serial.println("[TTS/PICO] Speaker 16 kHz configure failed");
    picotts_shutdown();
    return false;
  }

  kiraVoiceTtsSpeakerEnable(true);
  delay(35);

  mouthEnvelope = 0;
  elliVisualAudioSpeechBegin();

  // Include the trailing NUL: this tells PicoTTS the utterance is complete.
  picotts_add(
    text.c_str(),
    text.length() + 1
  );

  uint32_t timeoutMs =
    12000UL + (uint32_t)text.length() * 110UL;

  if(timeoutMs > 90000UL) timeoutMs = 90000UL;

  uint32_t startMs = millis();

  while(
    !picoDone &&
    millis() - startMs < timeoutMs
  ) {
    delay(5);
  }

  bool ok =
    picoDone &&
    !picoError &&
    !picoWriteError;

  if(!picoDone) {
    Serial.println("[TTS/PICO] Synthesis timeout");
  }

  elliVisualAudioSpeechLevel(0);
  kiraVoiceTtsSpeakerSilence();
  delay(25);
  kiraVoiceTtsSpeakerEnable(false);
  kiraVoiceTtsSpeakerRestore();
  elliVisualAudioSpeechEnd();

  picotts_shutdown();

  if(ok) {
    Serial.println("[TTS/HYBRID] PicoTTS offline succeeded");
  }
  else {
    Serial.println("[TTS/PICO] Offline synthesis failed");
  }

  return ok;
#endif
}

} // namespace


bool kiraTtsHybridConfigured() {
  return edgeConfigured() || picoConfigured();
}


bool kiraTtsHybridFallback(
  const String& text
) {
  if(
    WiFi.status() == WL_CONNECTED &&
    edgeConfigured()
  ) {
    if(speakEdge(text)) return true;
  }

  if(picoConfigured()) {
    if(speakPico(text)) return true;
  }

  return false;
}
