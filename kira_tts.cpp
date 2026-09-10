#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "kira_wifi_max.h"
#include <esp_heap_caps.h>

#include "kira_tts.h"
#include "kira_voice.h"
#include "kira_network_v2.h"
#include "kira_secrets.h"
#include "elli_visual.h"
#include "kira_tts_hybrid.h"
#include "kira_audio_output.h"
#include "kira_metrics.h"
#include "kira_next_config.h"

#ifndef KIRA_GROQ_API_KEY
#define KIRA_GROQ_API_KEY ""
#endif

// These are implemented by kira_voice.cpp so Phase 5L can reuse the
// already-proven I2S1/MAX98357A speaker without creating a second driver.
bool kiraVoiceTtsSpeakerConfigure(uint32_t sampleRate);
size_t kiraVoiceTtsSpeakerWrite(const uint8_t* data, size_t bytes);
void kiraVoiceTtsSpeakerEnable(bool enabled);
void kiraVoiceTtsSpeakerSilence();
void kiraVoiceTtsSpeakerRestore();
const char* kiraVoiceGroqApiKey();

namespace {

// ============================================================
// GLOBAL ELLI SPEECH QUEUE
// ============================================================

constexpr uint8_t TTS_PENDING_MAX = 8;

String ttsPending[TTS_PENDING_MAX];
uint8_t ttsPendingHead = 0;
uint8_t ttsPendingTail = 0;
uint8_t ttsPendingItems = 0;


// Number of characters from the normalized Elli reply that Groq has
// definitely finished playing. Used so a backup provider speaks ONLY
// the remaining text if Groq fails halfway through a long reply.
size_t groqSpokenChars = 0;


// Phase 5M: smoothed real-audio envelope used by Elli mouth frames.
uint32_t ttsMouthEnvelope = 0;



constexpr const char* TTS_URL =
  "https://api.groq.com/openai/v1/audio/speech";

constexpr const char* TTS_MODEL =
  "canopylabs/orpheus-v1-english";

// Phase 5L V1 default Elli voice. It can later become a persistent setting.
constexpr const char* TTS_VOICE = "autumn";

// Orpheus currently accepts at most 200 input characters per request.
// Keep a little margin for UTF-8 and future provider validation changes.
constexpr size_t TTS_CHUNK_LIMIT = 195;

// Phase 5L.7: this is a PSRAM PREFETCH BATCH size, NOT a total-answer cap.
// KIRA synthesizes up to 8 Orpheus chunks, plays/frees that batch, then
// continues with the next batch until the complete Elli reply is spoken.
constexpr uint8_t TTS_PREFETCH_BATCH_CHUNKS = 8;

constexpr uint32_t TTS_HTTP_TIMEOUT_MS = 30000;
constexpr uint32_t TTS_STREAM_TIMEOUT_MS = 8000;

// KIRA Next: a transient provider/network failure must not immediately
// amputate the rest of Elli's answer.
constexpr uint8_t TTS_REQUEST_MAX_ATTEMPTS = 5;
constexpr uint32_t TTS_RETRY_BASE_MS = 550;
constexpr uint32_t TTS_429_RETRY_BASE_MS = 2000;

int lastPreparedTtsHttpCode = 0;

// Groq Orpheus rate-limit state.
// A 429 must NOT trigger five expensive retries in the same turn.
uint32_t groqTtsCooldownUntilMs = 0;
uint32_t lastPreparedTtsRetryAfterMs = 0;


bool ttsDeadlinePending(
  uint32_t deadline
){
  if(deadline==0) return false;

  return
    (int32_t)(
      deadline-millis()
    )>0;
}


uint32_t ttsCooldownRemainingMs(){
  if(
    !ttsDeadlinePending(
      groqTtsCooldownUntilMs
    )
  ){
    return 0;
  }

  return
    groqTtsCooldownUntilMs-
    millis();
}

// Keep MAX98357A playback comfortably below full-scale PCM.
constexpr int32_t TTS_GAIN_NUM = 3;
constexpr int32_t TTS_GAIN_DEN = 5;


uint16_t readLe16(const uint8_t* p) {
  return
    (uint16_t)p[0] |
    ((uint16_t)p[1] << 8);
}


uint32_t readLe32(const uint8_t* p) {
  return
    (uint32_t)p[0] |
    ((uint32_t)p[1] << 8) |
    ((uint32_t)p[2] << 16) |
    ((uint32_t)p[3] << 24);
}


bool readExact(
  Stream& stream,
  uint8_t* out,
  size_t bytes,
  uint32_t timeoutMs = TTS_STREAM_TIMEOUT_MS
) {
  size_t got = 0;
  uint32_t lastData = millis();

  while (got < bytes) {
    int available = stream.available();

    if (available > 0) {
      size_t want = bytes - got;
      if ((size_t)available < want) want = (size_t)available;

      size_t n = stream.readBytes(
        (char*)out + got,
        want
      );

      if (n > 0) {
        got += n;
        lastData = millis();
        continue;
      }
    }

    if (millis() - lastData > timeoutMs) {
      return false;
    }

    delay(1);
  }

  return true;
}


bool skipBytes(
  Stream& stream,
  uint32_t bytes
) {
  uint8_t scratch[128];

  while (bytes > 0) {
    size_t n = bytes;
    if (n > sizeof(scratch)) n = sizeof(scratch);

    if (!readExact(stream, scratch, n)) {
      return false;
    }

    bytes -= (uint32_t)n;
  }

  return true;
}


String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 24);

  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];

    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += ' '; break;
      default:
        if ((uint8_t)c >= 0x20) out += c;
        break;
    }
  }

  return out;
}


String normalizeForSpeech(String text) {
  text.replace("\r", " ");
  text.replace("\n", " ");

  // Light Markdown cleanup. Keep punctuation that helps prosody.
  text.replace("**", "");
  text.replace("__", "");
  text.replace("`", "");
  text.replace("#", "");

  while (text.indexOf("  ") >= 0) {
    text.replace("  ", " ");
  }

  text.trim();
  return text;
}


size_t chooseChunkEnd(
  const String& text,
  size_t start
) {
  size_t remaining = text.length() - start;

  if (remaining <= TTS_CHUNK_LIMIT) {
    return text.length();
  }

  size_t hardEnd = start + TTS_CHUNK_LIMIT;
  size_t best = start;

  // Prefer sentence boundaries, then softer punctuation, then spaces.
  for (size_t i = start; i < hardEnd; i++) {
    char c = text[i];
    if (c == '.' || c == '?' || c == '!') {
      best = i + 1;
    }
  }

  if (best > start + 50) return best;

  for (size_t i = start; i < hardEnd; i++) {
    char c = text[i];
    if (c == ';' || c == ':' || c == ',') {
      best = i + 1;
    }
  }

  if (best > start + 50) return best;

  for (size_t i = start; i < hardEnd; i++) {
    if (text[i] == ' ') best = i;
  }

  if (best > start + 30) return best;

  return hardEnd;
}


int16_t scalePcm(int16_t sample) {
  int32_t v =
    ((int32_t)sample * TTS_GAIN_NUM) /
    TTS_GAIN_DEN;

  if (v > 32767) v = 32767;
  if (v < -32768) v = -32768;

  return (int16_t)v;
}


bool playPcmData(
  Stream& stream,
  uint32_t dataBytes,
  uint16_t channels,
  uint32_t sampleRate,
  uint16_t bitsPerSample
) {
  if (
    bitsPerSample != 16 ||
    (channels != 1 && channels != 2) ||
    sampleRate < 8000 ||
    sampleRate > 48000
  ) {
    Serial.print("[TTS] Unsupported WAV format: ");
    Serial.print(sampleRate);
    Serial.print(" Hz, ");
    Serial.print(bitsPerSample);
    Serial.print(" bit, channels=");
    Serial.println(channels);
    return false;
  }

  if (!kiraVoiceTtsSpeakerConfigure(sampleRate)) {
    Serial.println("[TTS] Speaker reconfigure failed");
    return false;
  }

  kiraVoiceTtsSpeakerEnable(true);
  delay(35);

  elliVisualSetState(ELLI_VISUAL_SPEAKING);

  int16_t mono[384];
  int16_t stereo[768];
  uint8_t rawStereo[1536];

  uint32_t remaining = dataBytes;
  bool ok = true;

  while (remaining > 0) {
    if (channels == 1) {
      size_t bytes = remaining;
      if (bytes > sizeof(mono)) bytes = sizeof(mono);
      bytes &= ~((size_t)1);

      if (bytes == 0) break;

      if (!readExact(stream, (uint8_t*)mono, bytes)) {
        ok = false;
        break;
      }

      size_t samples = bytes / sizeof(int16_t);

      for (size_t i = 0; i < samples; i++) {
        int16_t s = scalePcm(mono[i]);
        stereo[i * 2] = s;
        stereo[i * 2 + 1] = s;
      }

      size_t outBytes = samples * 2 * sizeof(int16_t);
      if (kiraVoiceTtsSpeakerWrite(
            (const uint8_t*)stereo,
            outBytes
          ) != outBytes) {
        ok = false;
        break;
      }

      remaining -= (uint32_t)bytes;
    }
    else {
      size_t bytes = remaining;
      if (bytes > sizeof(rawStereo)) bytes = sizeof(rawStereo);
      bytes &= ~((size_t)3);

      if (bytes == 0) break;

      if (!readExact(stream, rawStereo, bytes)) {
        ok = false;
        break;
      }

      int16_t* samples = (int16_t*)rawStereo;
      size_t count = bytes / sizeof(int16_t);

      for (size_t i = 0; i < count; i++) {
        samples[i] = scalePcm(samples[i]);
      }

      if (kiraVoiceTtsSpeakerWrite(rawStereo, bytes) != bytes) {
        ok = false;
        break;
      }

      remaining -= (uint32_t)bytes;
    }

    delay(0);
  }

  kiraVoiceTtsSpeakerSilence();
  delay(25);
  kiraVoiceTtsSpeakerEnable(false);
  kiraVoiceTtsSpeakerRestore();

  return ok && remaining == 0;
}



bool writeSpeakerAll(
  const uint8_t* data,
  size_t bytes
) {
  if(
    data == nullptr ||
    bytes == 0
  ) {
    return true;
  }

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
      Serial.println("[TTS] I2S write stalled");
      return false;
    }

    delay(1);
  }

  return true;
}


bool playPcmMemory(
  const uint8_t* pcm,
  uint32_t dataBytes,
  uint16_t channels,
  uint32_t sampleRate,
  uint16_t bitsPerSample
) {
  if(
    pcm == nullptr ||
    bitsPerSample != 16 ||
    (channels != 1 && channels != 2) ||
    sampleRate < 8000 ||
    sampleRate > 48000
  ) {
    Serial.print("[TTS] Unsupported buffered WAV format: ");
    Serial.print(sampleRate);
    Serial.print(" Hz, ");
    Serial.print(bitsPerSample);
    Serial.print(" bit, channels=");
    Serial.println(channels);
    return false;
  }

  if(!kiraVoiceTtsSpeakerConfigure(sampleRate)) {
    Serial.println("[TTS] Speaker reconfigure failed");
    return false;
  }

  kiraVoiceTtsSpeakerEnable(true);
  delay(35);

  elliVisualSetState(
    ELLI_VISUAL_SPEAKING
  );

  // Network is completely out of the playback path now.
  // These buffers only feed I2S from local memory.
  // Larger static DMA-feed staging buffer. 1024 stereo frames = 4 KiB.
  // Static storage avoids consuming the loop/task stack on every call.
  static int16_t stereoOut[2048];

  uint32_t offset = 0;
  bool ok = true;

  if(channels == 1) {
    while(offset + 1 < dataBytes) {
      size_t monoSamples =
        (dataBytes - offset) / sizeof(int16_t);

      if(monoSamples > 1024) {
        monoSamples = 1024;
      }

      const int16_t* monoIn =
        reinterpret_cast<const int16_t*>(
          pcm + offset
        );

      for(size_t i = 0; i < monoSamples; i++) {
        int16_t s =
          scalePcm(
            monoIn[i]
          );

        stereoOut[i * 2] =
          s;

        stereoOut[i * 2 + 1] =
          s;
      }

      size_t outputBytes =
        monoSamples *
        2 *
        sizeof(int16_t);

      if(
        !writeSpeakerAll(
          reinterpret_cast<const uint8_t*>(
            stereoOut
          ),
          outputBytes
        )
      ) {
        ok = false;
        break;
      }

      offset +=
        monoSamples *
        sizeof(int16_t);

    }
  }
  else {
    while(offset + 3 < dataBytes) {
      size_t frames =
        (dataBytes - offset) /
        (2 * sizeof(int16_t));

      if(frames > 1024) {
        frames = 1024;
      }

      const int16_t* stereoIn =
        reinterpret_cast<const int16_t*>(
          pcm + offset
        );

      for(size_t i = 0; i < frames * 2; i++) {
        stereoOut[i] =
          scalePcm(
            stereoIn[i]
          );
      }

      size_t outputBytes =
        frames *
        2 *
        sizeof(int16_t);

      if(
        !writeSpeakerAll(
          reinterpret_cast<const uint8_t*>(
            stereoOut
          ),
          outputBytes
        )
      ) {
        ok = false;
        break;
      }

      offset +=
        outputBytes;

    }
  }

  kiraVoiceTtsSpeakerSilence();
  delay(25);
  kiraVoiceTtsSpeakerEnable(false);
  kiraVoiceTtsSpeakerRestore();

  return ok;
}


bool playBufferedWav(
  const uint8_t* wav,
  size_t wavBytes
) {
  if(
    wav == nullptr ||
    wavBytes < 44
  ) {
    Serial.println("[TTS] Buffered WAV too small");
    return false;
  }

  if(
    memcmp(wav, "RIFF", 4) != 0 ||
    memcmp(wav + 8, "WAVE", 4) != 0
  ) {
    Serial.println("[TTS] Buffered response is not RIFF/WAVE");
    return false;
  }

  bool haveFormat = false;

  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;

  size_t pos = 12;

  while(pos + 8 <= wavBytes) {
    const uint8_t* header =
      wav + pos;

    uint32_t chunkBytes =
      readLe32(
        header + 4
      );

    pos += 8;

    size_t availableChunkBytes =
      wavBytes - pos;

    bool isDataChunk =
      memcmp(
        header,
        "data",
        4
      ) == 0;

    // Streaming WAV responses can use 0xFFFFFFFF (or another
    // intentionally oversized value) for the data length because
    // the final size was unknown when the header was generated.
    //
    // We already buffered the complete HTTP body, so for a data
    // chunk the bytes physically present in the buffer are the
    // authoritative length.
    if(
      chunkBytes >
      availableChunkBytes
    ) {

      if(
        isDataChunk
      ) {

        Serial.print(
          "[TTS] Streaming WAV data size placeholder: "
        );

        Serial.print(
          chunkBytes
        );

        Serial.print(
          " -> using buffered bytes "
        );

        Serial.println(
          availableChunkBytes
        );

        chunkBytes =
          (uint32_t)availableChunkBytes;
      }
      else {

        Serial.println(
          "[TTS] WAV chunk exceeds buffer"
        );

        return false;
      }
    }

    if(
      memcmp(
        header,
        "fmt ",
        4
      ) == 0
    ) {
      if(chunkBytes < 16) {
        Serial.println("[TTS] Invalid WAV fmt chunk");
        return false;
      }

      const uint8_t* fmt =
        wav + pos;

      audioFormat =
        readLe16(
          fmt + 0
        );

      channels =
        readLe16(
          fmt + 2
        );

      sampleRate =
        readLe32(
          fmt + 4
        );

      bitsPerSample =
        readLe16(
          fmt + 14
        );

      haveFormat = true;

      Serial.print("[TTS] WAV ");
      Serial.print(sampleRate);
      Serial.print(" Hz / ");
      Serial.print(bitsPerSample);
      Serial.print(" bit / ch=");
      Serial.println(channels);
    }
    else if(
      memcmp(
        header,
        "data",
        4
      ) == 0
    ) {
      if(
        !haveFormat ||
        audioFormat != 1
      ) {
        Serial.println("[TTS] WAV PCM format unavailable");
        return false;
      }

      Serial.print("[TTS] Buffered PCM bytes: ");
      Serial.println(chunkBytes);

      return playPcmMemory(
        wav + pos,
        chunkBytes,
        channels,
        sampleRate,
        bitsPerSample
      );
    }

    pos += chunkBytes;

    if(
      chunkBytes & 1U
    ) {
      if(pos >= wavBytes) {
        return false;
      }

      pos++;
    }
  }

  Serial.println("[TTS] WAV data chunk not found");
  return false;
}


bool downloadAndPlayBufferedWav(
  HTTPClient& https
) {
  NetworkClient* stream =
    https.getStreamPtr();

  if(
    stream == nullptr
  ) {

    Serial.println(
      "[TTS] No HTTP audio stream"
    );

    return false;
  }


  stream->setTimeout(
    TTS_STREAM_TIMEOUT_MS
  );


  const size_t reservePsram =
    768UL * 1024UL;

  const size_t hardMaxWav =
    3UL * 1024UL * 1024UL;

  const size_t freePsram =
    ESP.getFreePsram();


  if(
    freePsram <=
    reservePsram + 44
  ) {

    Serial.print(
      "[TTS] Not enough PSRAM for WAV. free="
    );

    Serial.println(
      freePsram
    );

    return false;
  }


  size_t safeCapacity =
    freePsram -
    reservePsram;


  if(
    safeCapacity >
    hardMaxWav
  ) {

    safeCapacity =
      hardMaxWav;
  }


  int announcedSize =
    https.getSize();


  // ==========================================================
  // NORMAL HTTP RESPONSE: KNOWN LENGTH
  // ==========================================================

  if(
    announcedSize > 0
  ) {

    size_t wavBytes =
      static_cast<size_t>(
        announcedSize
      );


    if(
      wavBytes < 44 ||
      wavBytes >
      safeCapacity
    ) {

      Serial.print(
        "[TTS] WAV size unsafe: "
      );

      Serial.print(
        wavBytes
      );

      Serial.print(
        " bytes, safe capacity="
      );

      Serial.println(
        safeCapacity
      );

      return false;
    }


    uint8_t* wav =
      static_cast<uint8_t*>(
        heap_caps_malloc(
          wavBytes,
          MALLOC_CAP_SPIRAM |
          MALLOC_CAP_8BIT
        )
      );


    if(
      wav == nullptr
    ) {

      Serial.println(
        "[TTS] PSRAM WAV allocation failed"
      );

      return false;
    }


    Serial.print(
      "[TTS] Buffering WAV in PSRAM: "
    );

    Serial.print(
      wavBytes
    );

    Serial.println(
      " bytes"
    );


    bool downloaded =
      readExact(
        *stream,
        wav,
        wavBytes,
        TTS_HTTP_TIMEOUT_MS
      );


    if(
      !downloaded
    ) {

      Serial.println(
        "[TTS] WAV download timed out"
      );

      heap_caps_free(
        wav
      );

      return false;
    }


    Serial.println(
      "[TTS] WAV buffered; starting uninterrupted playback"
    );


    bool played =
      playBufferedWav(
        wav,
        wavBytes
      );


    heap_caps_free(
      wav
    );


    return played;
  }


  // ==========================================================
  // STREAMING RESPONSE: NO CONTENT-LENGTH
  // ==========================================================
  //
  // Groq can return a valid streaming WAV using placeholder
  // RIFF/data sizes (commonly 0xFFFFFFFF). In that case neither
  // HTTP Content-Length nor the RIFF size field is authoritative.
  //
  // Allocate a bounded PSRAM receive buffer and count the bytes
  // actually delivered by the HTTP connection.
  // ==========================================================

  Serial.println(
    "[TTS] No HTTP Content-Length; buffering stream until end"
  );


  uint8_t* wav =
    static_cast<uint8_t*>(
      heap_caps_malloc(
        safeCapacity,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      )
    );


  if(
    wav == nullptr
  ) {

    Serial.println(
      "[TTS] Streaming WAV PSRAM allocation failed"
    );

    return false;
  }


  size_t wavBytes = 0;

  uint32_t lastDataMs =
    millis();

  bool hitCapacity =
    false;


  while(
    wavBytes <
    safeCapacity
  ) {

    int available =
      stream->available();


    if(
      available > 0
    ) {

      size_t room =
        safeCapacity -
        wavBytes;

      size_t want =
        (size_t)available;


      if(
        want >
        room
      ) {

        want =
          room;
      }


      size_t n =
        stream->readBytes(
          reinterpret_cast<char*>(
            wav + wavBytes
          ),
          want
        );


      if(
        n > 0
      ) {

        wavBytes +=
          n;

        lastDataMs =
          millis();

        continue;
      }
    }


    // HTTP/1.0 responses without Content-Length normally terminate
    // by closing the connection. Once all currently buffered bytes
    // have been consumed, disconnected means true end-of-body.
    if(
      !stream->connected() &&
      stream->available() <= 0
    ) {

      break;
    }


    // Some TLS stacks keep the socket object alive briefly after the
    // final body byte. Treat a quiet interval as end-of-stream once a
    // plausible WAV body has already been received.
    if(
      wavBytes >= 44 &&
      millis() - lastDataMs >
      TTS_STREAM_TIMEOUT_MS
    ) {

      Serial.println(
        "[TTS] Streaming WAV quiet end detected"
      );

      break;
    }


    // Before even receiving a WAV header, a long silence is a failure.
    if(
      wavBytes < 44 &&
      millis() - lastDataMs >
      TTS_HTTP_TIMEOUT_MS
    ) {

      Serial.println(
        "[TTS] Streaming WAV header timeout"
      );

      heap_caps_free(
        wav
      );

      return false;
    }


    delay(
      1
    );
  }


  if(
    wavBytes >=
    safeCapacity
  ) {

    hitCapacity =
      true;
  }


  if(
    hitCapacity
  ) {

    Serial.print(
      "[TTS] Streaming WAV exceeded safe buffer capacity: "
    );

    Serial.println(
      safeCapacity
    );

    heap_caps_free(
      wav
    );

    return false;
  }


  if(
    wavBytes < 44
  ) {

    Serial.print(
      "[TTS] Streaming WAV too small: "
    );

    Serial.println(
      wavBytes
    );

    heap_caps_free(
      wav
    );

    return false;
  }


  if(
    memcmp(
      wav,
      "RIFF",
      4
    ) != 0 ||
    memcmp(
      wav + 8,
      "WAVE",
      4
    ) != 0
  ) {

    Serial.println(
      "[TTS] Streaming response is not RIFF/WAVE"
    );

    heap_caps_free(
      wav
    );

    return false;
  }


  uint32_t riffSizeField =
    readLe32(
      wav + 4
    );


  Serial.print(
    "[TTS] Streaming WAV buffered bytes: "
  );

  Serial.println(
    wavBytes
  );


  Serial.print(
    "[TTS] RIFF size field: "
  );

  Serial.println(
    riffSizeField
  );


  if(
    riffSizeField ==
    0xFFFFFFFFUL
  ) {

    Serial.println(
      "[TTS] Streaming RIFF placeholder accepted"
    );
  }


  Serial.println(
    "[TTS] WAV buffered; starting uninterrupted playback"
  );


  bool played =
    playBufferedWav(
      wav,
      wavBytes
    );


  heap_caps_free(
    wav
  );


  return played;
}

bool streamWavFromHttp(
  HTTPClient& https
) {
  NetworkClient* stream = https.getStreamPtr();

  if (!stream) {
    Serial.println("[TTS] No HTTP audio stream");
    return false;
  }

  stream->setTimeout(TTS_STREAM_TIMEOUT_MS);

  uint8_t riff[12];

  if (!readExact(*stream, riff, sizeof(riff))) {
    Serial.println("[TTS] WAV header timeout");
    return false;
  }

  if (
    memcmp(riff, "RIFF", 4) != 0 ||
    memcmp(riff + 8, "WAVE", 4) != 0
  ) {
    Serial.println("[TTS] Response is not RIFF/WAVE");
    return false;
  }

  bool haveFormat = false;
  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;

  for (uint8_t chunkIndex = 0; chunkIndex < 24; chunkIndex++) {
    uint8_t chunkHeader[8];

    if (!readExact(*stream, chunkHeader, sizeof(chunkHeader))) {
      Serial.println("[TTS] WAV chunk header timeout");
      return false;
    }

    uint32_t chunkBytes = readLe32(chunkHeader + 4);

    if (memcmp(chunkHeader, "fmt ", 4) == 0) {
      if (chunkBytes < 16) {
        Serial.println("[TTS] Invalid WAV fmt chunk");
        return false;
      }

      uint8_t fmt[16];

      if (!readExact(*stream, fmt, sizeof(fmt))) {
        return false;
      }

      audioFormat = readLe16(fmt + 0);
      channels = readLe16(fmt + 2);
      sampleRate = readLe32(fmt + 4);
      bitsPerSample = readLe16(fmt + 14);

      if (chunkBytes > sizeof(fmt)) {
        if (!skipBytes(*stream, chunkBytes - sizeof(fmt))) {
          return false;
        }
      }

      if (chunkBytes & 1U) {
        uint8_t pad;
        if (!readExact(*stream, &pad, 1)) return false;
      }

      haveFormat = true;

      Serial.print("[TTS] WAV ");
      Serial.print(sampleRate);
      Serial.print(" Hz / ");
      Serial.print(bitsPerSample);
      Serial.print(" bit / ch=");
      Serial.println(channels);

      continue;
    }

    if (memcmp(chunkHeader, "data", 4) == 0) {
      if (!haveFormat || audioFormat != 1) {
        Serial.println("[TTS] WAV PCM format unavailable");
        return false;
      }

      return playPcmData(
        *stream,
        chunkBytes,
        channels,
        sampleRate,
        bitsPerSample
      );
    }

    if (!skipBytes(*stream, chunkBytes)) {
      return false;
    }

    if (chunkBytes & 1U) {
      uint8_t pad;
      if (!readExact(*stream, &pad, 1)) return false;
    }
  }

  Serial.println("[TTS] WAV data chunk not found");
  return false;
}


bool requestAndPlayChunk(
  const String& text,
  uint8_t index,
  uint8_t totalHint
) {
  KiraWifiRequestScope netScope(
    KIRA_NET_PRIORITY_TTS,
    "Groq TTS"
  );

  String payload;
  payload.reserve(text.length() + 220);

  payload += "{\"model\":\"";
  payload += TTS_MODEL;
  payload += "\",\"input\":\"";
  payload += jsonEscape(text);
  payload += "\",\"voice\":\"";
  payload += TTS_VOICE;
  payload += "\",\"response_format\":\"wav\"}";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.useHTTP10(true);
  https.setTimeout(kiraWifiMaxAdaptiveTimeout(TTS_HTTP_TIMEOUT_MS));
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!https.begin(client, TTS_URL)) {
    Serial.println("[TTS] HTTPS begin failed");
    return false;
  }

  const char* responseHeaders[]={
    "retry-after"
  };

  https.collectHeaders(
    responseHeaders,
    1
  );


  https.addHeader(
    "Authorization",
    "Bearer " + String(kiraVoiceGroqApiKey())
  );

  https.addHeader(
    "Content-Type",
    "application/json"
  );

  https.addHeader(
    "Accept",
    "audio/wav"
  );

  // KIRA Group 1 V2.6:
  // For responses without Content-Length, wait for a real connection close.
  // Explicitly asking for close-delimited HTTP prevents us from guessing the
  // end of a generated WAV from a short network pause.
  https.addHeader(
    "Connection",
    "close"
  );

  Serial.print("[TTS] Request chunk ");
  Serial.print(index);
  if (totalHint > 0) {
    Serial.print("/");
    Serial.print(totalHint);
  }
  Serial.print(" chars=");
  Serial.println(text.length());

  int code = https.POST(
    (uint8_t*)payload.c_str(),
    payload.length()
  );


  netScope.setHttpCode(
    code
  );

  netScope.setSuccess(
    code>=200 &&
    code<300
  );


  Serial.print("[TTS] HTTP ");
  Serial.println(code);

  if (code < 200 || code >= 300) {
    String errorText;
    if (code > 0) errorText = https.getString();
    https.end();

    errorText.trim();
    if (errorText.length() > 180) {
      errorText.remove(180);
      errorText += "...";
    }

    if (errorText.length()) {
      Serial.print("[TTS] Provider error: ");
      Serial.println(errorText);
    }

    return false;
  }

  bool ok =
    downloadAndPlayBufferedWav(
      https
    );

  https.end();

  return ok;
}



// ============================================================
// PHASE 5L.2C - PREFETCH + SEAMLESS MULTI-CHUNK PLAYBACK
// ============================================================
//
// Orpheus limits each request to about 200 characters. Previously
// KIRA did this for every chunk:
//
// request -> download -> play -> mute -> request -> download -> play
//
// That necessarily creates an audible break on long replies.
//
// This engine instead does:
//
// request all chunks -> keep WAVs in PSRAM -> one I2S session ->
// play every PCM chunk back-to-back -> mute once at the very end.
// ============================================================

struct PreparedTtsChunk {
  uint8_t* wav = nullptr;
  size_t wavBytes = 0;

  const uint8_t* pcm = nullptr;
  uint32_t pcmBytes = 0;

  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
};


void freePreparedTtsChunk(
  PreparedTtsChunk& chunk
) {

  if(
    chunk.wav != nullptr
  ) {

    heap_caps_free(
      chunk.wav
    );
  }


  chunk =
    PreparedTtsChunk();
}


bool parsePreparedTtsChunk(
  PreparedTtsChunk& chunk
) {

  if(
    chunk.wav == nullptr ||
    chunk.wavBytes < 44
  ) {
    return false;
  }


  if(
    memcmp(
      chunk.wav,
      "RIFF",
      4
    ) != 0 ||
    memcmp(
      chunk.wav + 8,
      "WAVE",
      4
    ) != 0
  ) {

    Serial.println(
      "[TTS/PREFETCH] Buffered response is not RIFF/WAVE"
    );

    return false;
  }


  bool haveFormat =
    false;

  uint16_t audioFormat =
    0;

  size_t pos =
    12;


  while(
    pos + 8 <=
    chunk.wavBytes
  ) {

    const uint8_t* header =
      chunk.wav + pos;

    uint32_t chunkBytes =
      readLe32(
        header + 4
      );

    pos +=
      8;


    size_t availableBytes =
      chunk.wavBytes - pos;


    bool isData =
      memcmp(
        header,
        "data",
        4
      ) == 0;


    if(
      chunkBytes >
      availableBytes
    ) {

      if(
        isData
      ) {

        chunkBytes =
          (uint32_t)availableBytes;
      }
      else {

        Serial.println(
          "[TTS/PREFETCH] Invalid WAV chunk size"
        );

        return false;
      }
    }


    if(
      memcmp(
        header,
        "fmt ",
        4
      ) == 0
    ) {

      if(
        chunkBytes < 16
      ) {
        return false;
      }


      const uint8_t* fmt =
        chunk.wav + pos;


      audioFormat =
        readLe16(
          fmt + 0
        );

      chunk.channels =
        readLe16(
          fmt + 2
        );

      chunk.sampleRate =
        readLe32(
          fmt + 4
        );

      chunk.bitsPerSample =
        readLe16(
          fmt + 14
        );

      haveFormat =
        true;
    }
    else if(
      isData
    ) {

      if(
        !haveFormat ||
        audioFormat != 1 ||
        chunk.bitsPerSample != 16 ||
        (
          chunk.channels != 1 &&
          chunk.channels != 2
        ) ||
        chunk.sampleRate < 8000 ||
        chunk.sampleRate > 48000
      ) {

        Serial.println(
          "[TTS/PREFETCH] Unsupported PCM WAV format"
        );

        return false;
      }


      chunk.pcm =
        chunk.wav + pos;

      chunk.pcmBytes =
        chunkBytes;


      return
        chunk.pcmBytes >=
        chunk.channels * 2;
    }


    pos +=
      chunkBytes;


    if(
      chunkBytes & 1U
    ) {

      if(
        pos >=
        chunk.wavBytes
      ) {
        return false;
      }

      pos++;
    }
  }


  Serial.println(
    "[TTS/PREFETCH] WAV data chunk not found"
  );

  return false;
}


uint32_t pcmFramePeak(
  const uint8_t* frame,
  uint16_t channels
) {

  const int16_t* samples =
    reinterpret_cast<const int16_t*>(
      frame
    );


  int32_t peak =
    0;


  for(
    uint16_t ch = 0;
    ch < channels;
    ch++
  ) {

    int32_t v =
      samples[ch];

    if(
      v < 0
    ) {
      v = -v;
    }

    if(
      v > peak
    ) {
      peak = v;
    }
  }


  return
    (uint32_t)peak;
}


void trimInternalChunkSilence(
  PreparedTtsChunk& chunk,
  bool trimLeading,
  bool trimTrailing
) {

  if(
    chunk.pcm == nullptr ||
    chunk.pcmBytes < 8
  ) {
    return;
  }


  const size_t frameBytes =
    chunk.channels *
    sizeof(int16_t);

  size_t frames =
    chunk.pcmBytes /
    frameBytes;


  if(
    frames < 4
  ) {
    return;
  }


  // KIRA TTS Continuity V2.2:
  // Orpheus WAV chunks may contain a noticeable low-level lead-in/tail even
  // when it is not digital zero. A threshold of 96 was too conservative and
  // left large pauses between independent TTS requests.
  //
  // Keep only ~8 ms around INTERNAL joins. The first and final answer edges
  // are not both trimmed, so natural response boundaries remain.
  const uint32_t threshold =
    320;

  size_t keepFrames =
    (size_t)(
      chunk.sampleRate *
      8UL /
      1000UL
    );


  if(
    keepFrames < 1
  ) {
    keepFrames = 1;
  }


  size_t firstVoice =
    0;


  if(
    trimLeading
  ) {

    while(
      firstVoice < frames &&
      pcmFramePeak(
        chunk.pcm +
          firstVoice *
          frameBytes,
        chunk.channels
      ) <= threshold
    ) {

      firstVoice++;
    }


    if(
      firstVoice >
      keepFrames
    ) {

      size_t removeFrames =
        firstVoice -
        keepFrames;

      // Never trim more than 650 ms from a generated chunk. If Orpheus
      // produced something stranger than that, preserve it rather than risk
      // cutting valid speech.
      size_t maxTrimFrames =
        (size_t)(
          chunk.sampleRate *
          650UL /
          1000UL
        );

      if(
        removeFrames >
        maxTrimFrames
      ) {
        removeFrames =
          maxTrimFrames;
      }

      chunk.pcm +=
        removeFrames *
        frameBytes;

      chunk.pcmBytes -=
        (uint32_t)(
          removeFrames *
          frameBytes
        );

      frames -=
        removeFrames;
    }
  }


  if(
    trimTrailing &&
    frames > 2
  ) {

    size_t lastVoiceExclusive =
      frames;


    while(
      lastVoiceExclusive > 0 &&
      pcmFramePeak(
        chunk.pcm +
          (lastVoiceExclusive - 1) *
          frameBytes,
        chunk.channels
      ) <= threshold
    ) {

      lastVoiceExclusive--;
    }


    size_t desiredEnd =
      lastVoiceExclusive +
      keepFrames;


    if(
      desiredEnd < frames
    ) {
      size_t maxTrimFrames =
        (size_t)(
          chunk.sampleRate *
          650UL /
          1000UL
        );

      size_t proposedTrim =
        frames -
        desiredEnd;

      if(
        proposedTrim >
        maxTrimFrames
      ) {
        desiredEnd =
          frames -
          maxTrimFrames;
      }

      frames =
        desiredEnd;

      chunk.pcmBytes =
        (uint32_t)(
          frames *
          frameBytes
        );
    }
  }
}


bool growStreamingWavBuffer(
  uint8_t*& wav,
  size_t& capacity,
  size_t required,
  size_t safeMax
) {

  if(
    required <=
    capacity
  ) {
    return true;
  }


  size_t next =
    capacity;


  while(
    next < required
  ) {

    size_t doubled =
      next * 2;


    if(
      doubled <= next
    ) {
      return false;
    }


    next =
      doubled;


    if(
      next > safeMax
    ) {

      next =
        safeMax;

      break;
    }
  }


  if(
    next < required
  ) {
    return false;
  }


  uint8_t* grown =
    static_cast<uint8_t*>(
      heap_caps_realloc(
        wav,
        next,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      )
    );


  if(
    grown == nullptr
  ) {
    return false;
  }


  wav =
    grown;

  capacity =
    next;


  return true;
}


bool downloadWavToPsram(
  HTTPClient& https,
  uint8_t*& outWav,
  size_t& outBytes
) {

  outWav =
    nullptr;

  outBytes =
    0;


  NetworkClient* stream =
    https.getStreamPtr();


  if(
    stream == nullptr
  ) {
    return false;
  }


  stream->setTimeout(
    TTS_STREAM_TIMEOUT_MS
  );


  const size_t reservePsram =
    640UL * 1024UL;

  size_t freePsram =
    ESP.getFreePsram();


  if(
    freePsram <=
    reservePsram + 64UL * 1024UL
  ) {

    Serial.println(
      "[TTS/PREFETCH] Not enough free PSRAM"
    );

    return false;
  }


  size_t safeMax =
    freePsram -
    reservePsram;


  // A single short Orpheus chunk should be far below this. Keeping
  // the per-chunk cap bounded prevents one malformed response from
  // consuming the whole machine.
  const size_t perChunkHardMax =
    1400UL * 1024UL;


  if(
    safeMax >
    perChunkHardMax
  ) {
    safeMax =
      perChunkHardMax;
  }


  int announced =
    https.getSize();


  Serial.print(
    "[TTS/PREFETCH] Content-Length="
  );

  Serial.println(
    announced
  );


  if(
    announced > 0
  ) {

    size_t bytes =
      (size_t)announced;


    if(
      bytes < 44 ||
      bytes > safeMax
    ) {
      return false;
    }


    uint8_t* wav =
      static_cast<uint8_t*>(
        heap_caps_malloc(
          bytes,
          MALLOC_CAP_SPIRAM |
          MALLOC_CAP_8BIT
        )
      );


    if(
      wav == nullptr
    ) {
      return false;
    }


    if(
      !readExact(
        *stream,
        wav,
        bytes,
        TTS_HTTP_TIMEOUT_MS
      )
    ) {

      heap_caps_free(
        wav
      );

      return false;
    }


    outWav =
      wav;

    outBytes =
      bytes;


    return true;
  }


  size_t capacity =
    96UL * 1024UL;


  if(
    capacity >
    safeMax
  ) {
    capacity =
      safeMax;
  }


  uint8_t* wav =
    static_cast<uint8_t*>(
      heap_caps_malloc(
        capacity,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      )
    );


  if(
    wav == nullptr
  ) {
    return false;
  }


  size_t bytes =
    0;

  uint32_t lastDataMs =
    millis();


  while(true) {

    int available =
      stream->available();


    if(
      available > 0
    ) {

      size_t required =
        bytes +
        (size_t)available;


      if(
        !growStreamingWavBuffer(
          wav,
          capacity,
          required,
          safeMax
        )
      ) {

        Serial.println(
          "[TTS/PREFETCH] WAV exceeded safe PSRAM capacity"
        );

        heap_caps_free(
          wav
        );

        return false;
      }


      size_t n =
        stream->readBytes(
          reinterpret_cast<char*>(
            wav + bytes
          ),
          (size_t)available
        );


      if(
        n > 0
      ) {

        bytes +=
          n;

        lastDataMs =
          millis();

        continue;
      }
    }


    if(
      !stream->connected() &&
      stream->available() <= 0
    ) {
      break;
    }


    // KIRA Group 1 V2.6:
    // DO NOT treat a 1.2 s pause in incoming TLS bytes as end-of-WAV.
    // Orpheus may pause while generating the remainder of a longer request.
    // The old shortcut produced valid-looking but incomplete WAV files,
    // which is why Elli sometimes spoke only the first few words.
    //
    // HTTP/1.0 + Connection: close is the preferred end signal. This
    // 8-second no-data fallback exists only for a connection that refuses
    // to close cleanly.
    if(
      bytes >= 44 &&
      millis() - lastDataMs >
        TTS_STREAM_TIMEOUT_MS
    ) {
      Serial.println(
        "[TTS/PREFETCH] Stream quiet timeout after 8 s; using received body"
      );

      break;
    }


    if(
      bytes < 44 &&
      millis() - lastDataMs > TTS_HTTP_TIMEOUT_MS
    ) {

      heap_caps_free(
        wav
      );

      return false;
    }


    delay(
      1
    );
  }


  if(
    bytes < 44
  ) {

    heap_caps_free(
      wav
    );

    return false;
  }


  // Shrink the allocation so subsequent prefetched chunks have the
  // maximum possible PSRAM available.
  uint8_t* shrunk =
    static_cast<uint8_t*>(
      heap_caps_realloc(
        wav,
        bytes,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      )
    );


  if(
    shrunk != nullptr
  ) {
    wav =
      shrunk;
  }


  outWav =
    wav;

  outBytes =
    bytes;


  return true;
}


size_t countTtsWords(
  const String& text
) {
  size_t words=0;
  bool inside=false;

  for(
    size_t i=0;
    i<text.length();
    i++
  ){
    char c=
      text[i];

    bool alphaNumeric=
      isalnum(
        (unsigned char)c
      );

    if(
      alphaNumeric &&
      !inside
    ){
      words++;
      inside=true;
    }
    else if(
      !alphaNumeric
    ){
      inside=false;
    }
  }

  return words;
}


uint32_t preparedChunkDurationMs(
  const PreparedTtsChunk& chunk
) {
  if(
    chunk.sampleRate==0 ||
    chunk.channels==0 ||
    chunk.bitsPerSample==0
  ){
    return 0;
  }

  uint32_t bytesPerSample=
    chunk.bitsPerSample/
    8UL;

  if(
    bytesPerSample==0
  ){
    return 0;
  }

  uint64_t bytesPerSecond=
    (uint64_t)chunk.sampleRate*
    chunk.channels*
    bytesPerSample;

  if(
    bytesPerSecond==0
  ){
    return 0;
  }

  return
    (uint32_t)(
      (
        (uint64_t)chunk.pcmBytes*
        1000ULL
      )/
      bytesPerSecond
    );
}


bool requestPreparedTtsChunkOnce(
  const String& text,
  uint16_t index,
  PreparedTtsChunk& outChunk
) {

  KiraWifiRequestScope netScope(
    KIRA_NET_PRIORITY_TTS,
    "Groq TTS"
  );


  String payload;
  payload.reserve(
    text.length() + 220
  );

  payload +=
    "{\"model\":\"";

  payload +=
    TTS_MODEL;

  payload +=
    "\",\"input\":\"";

  payload +=
    jsonEscape(
      text
    );

  payload +=
    "\",\"voice\":\"";

  payload +=
    TTS_VOICE;

  payload +=
    "\",\"response_format\":\"wav\"}";


  WiFiClientSecure client;
  client.setInsecure();


  HTTPClient https;
  https.useHTTP10(
    true
  );

  https.setTimeout(
    kiraWifiMaxAdaptiveTimeout(
      TTS_HTTP_TIMEOUT_MS
    )
  );

  https.setFollowRedirects(
    HTTPC_STRICT_FOLLOW_REDIRECTS
  );


  if(
    !https.begin(
      client,
      TTS_URL
    )
  ) {
    return false;
  }


  https.addHeader(
    "Authorization",
    "Bearer " +
      String(
        kiraVoiceGroqApiKey()
      )
  );

  https.addHeader(
    "Content-Type",
    "application/json"
  );

  https.addHeader(
    "Accept",
    "audio/wav"
  );


  Serial.print(
    "[TTS/PREFETCH] Request chunk "
  );

  Serial.print(
    index
  );

  Serial.print(
    " chars="
  );

  Serial.println(
    text.length()
  );


  int code =
    https.POST(
      reinterpret_cast<uint8_t*>(
        const_cast<char*>(
          payload.c_str()
        )
      ),
      payload.length()
    );


  lastPreparedTtsHttpCode =
    code;


  netScope.setHttpCode(
    code
  );

  netScope.setSuccess(
    code>=200 &&
    code<300
  );


  Serial.print(
    "[TTS/PREFETCH] HTTP "
  );

  Serial.println(
    code
  );


  lastPreparedTtsRetryAfterMs=
    0;


  if(
    code==
    429
  ){
    String retryAfter=
      https.header(
        "retry-after"
      );

    retryAfter.trim();

    uint32_t seconds=
      (uint32_t)retryAfter.toInt();

    if(seconds<1){
      seconds=60;
    }

    // Respect provider reset timing without allowing a corrupt header to
    // create an absurd multi-day millis deadline.
    if(seconds>86400UL){
      seconds=86400UL;
    }

    lastPreparedTtsRetryAfterMs=
      seconds*
      1000UL;

    groqTtsCooldownUntilMs=
      millis()+
      lastPreparedTtsRetryAfterMs;

    Serial.print(
      "[TTS/RATE] Orpheus 429 | retry-after="
    );

    Serial.print(
      seconds
    );

    Serial.println(
      "s | entering cooldown"
    );
  }


  if(
    code < 200 ||
    code >= 300
  ) {

    if(
      code > 0
    ) {

      String errorText =
        https.getString();

      errorText.trim();

      if(
        errorText.length() > 160
      ) {
        errorText.remove(
          160
        );
      }

      if(
        errorText.length()
      ) {
        Serial.print(
          "[TTS/PREFETCH] Provider error: "
        );
        Serial.println(
          errorText
        );
      }
    }


    https.end();
    return false;
  }


  bool downloaded =
    downloadWavToPsram(
      https,
      outChunk.wav,
      outChunk.wavBytes
    );


  https.end();


  if(
    !downloaded
  ) {
    return false;
  }


  if(
    !parsePreparedTtsChunk(
      outChunk
    )
  ) {

    freePreparedTtsChunk(
      outChunk
    );

    return false;
  }


  uint32_t audioMs=
    preparedChunkDurationMs(
      outChunk
    );

  size_t wordCount=
    countTtsWords(
      text
    );

  // Extremely conservative lower bound. Even unusually fast natural speech
  // cannot pronounce a long chunk in only a second or two.
  uint32_t minimumExpectedMs=
    (uint32_t)(
      wordCount*
      110UL
    );

  if(
    minimumExpectedMs<
    300UL
  ){
    minimumExpectedMs=
      300UL;
  }


  Serial.print(
    "[TTS/PREFETCH] Audio duration="
  );

  Serial.print(
    audioMs
  );

  Serial.print(
    "ms words="
  );

  Serial.print(
    wordCount
  );

  Serial.print(
    " minimum_sane="
  );

  Serial.print(
    minimumExpectedMs
  );

  Serial.println(
    "ms"
  );


  if(
    wordCount>=5 &&
    audioMs<
      minimumExpectedMs
  ){
    Serial.println(
      "[TTS/PREFETCH] INCOMPLETE_WAV detected; retrying instead of speaking truncated audio"
    );

    freePreparedTtsChunk(
      outChunk
    );

    return false;
  }


  Serial.print(
    "[TTS/PREFETCH] Ready chunk "
  );

  Serial.print(
    index
  );

  Serial.print(
    " | WAV="
  );

  Serial.print(
    outChunk.wavBytes
  );

  Serial.print(
    " | PCM="
  );

  Serial.println(
    outChunk.pcmBytes
  );


  return true;
}





bool requestPreparedTtsChunk(
  const String& text,
  uint16_t index,
  PreparedTtsChunk& outChunk
) {

  uint32_t cooldown=
    ttsCooldownRemainingMs();

  if(cooldown>0){
    Serial.print(
      "[TTS/RATE] Orpheus cooldown active; skip request for "
    );

    Serial.print(
      (cooldown+999UL)/1000UL
    );

    Serial.println(
      "s"
    );

    lastPreparedTtsHttpCode=
      429;

    return false;
  }


  for(
    uint8_t attempt = 1;
    attempt <= TTS_REQUEST_MAX_ATTEMPTS;
    attempt++
  ) {

    freePreparedTtsChunk(
      outChunk
    );

    lastPreparedTtsHttpCode =
      0;

    uint32_t started =
      millis();


    if(
      requestPreparedTtsChunkOnce(
        text,
        index,
        outChunk
      )
    ) {

      kiraMetricsObserveLatency(
        "tts_chunk",
        millis() - started
      );

      if(
        attempt > 1
      ) {
        Serial.print(
          "[TTS/NEXT] chunk recovered on attempt "
        );

        Serial.println(
          attempt
        );
      }

      return true;
    }


    // Rate limits are not transient packet loss. Groq supplies Retry-After,
    // so hammering the same model four more times only wastes time.
    if(
      lastPreparedTtsHttpCode==
      429
    ){
      Serial.println(
        "[TTS/RATE] 429 received - aborting retries and switching to fallback immediately"
      );

      break;
    }


    if(
      attempt >=
      TTS_REQUEST_MAX_ATTEMPTS
    ) {
      break;
    }


    uint32_t waitMs =
      TTS_RETRY_BASE_MS *
      attempt;


    Serial.print(
      "[TTS/NEXT] retry chunk "
    );

    Serial.print(
      index
    );

    Serial.print(
      " attempt="
    );

    Serial.print(
      attempt + 1
    );

    Serial.print(
      " after="
    );

    Serial.print(
      waitMs
    );

    Serial.println(
      "ms"
    );


    delay(
      waitMs
    );
  }


  kiraMetricsIncrement(
    KIRA_METRIC_TTS_FAILURES
  );


  return false;
}


uint8_t mouthLevelFromPcm(
  const int16_t* samples,
  size_t sampleCount
) {

  if(
    samples == nullptr ||
    sampleCount == 0
  ) {
    return 0;
  }


  uint64_t sumAbs =
    0;

  uint32_t peak =
    0;


  // Sampling every other value is plenty for a visual envelope and
  // keeps this lightweight on the audio path.
  size_t used =
    0;


  for(
    size_t i = 0;
    i < sampleCount;
    i += 2
  ) {

    int32_t v =
      samples[i];


    if(
      v < 0
    ) {
      v = -v;
    }


    if(
      v > 32767
    ) {
      v = 32767;
    }


    sumAbs +=
      (uint32_t)v;


    if(
      (uint32_t)v > peak
    ) {
      peak =
        (uint32_t)v;
    }


    used++;
  }


  if(
    used == 0
  ) {
    return 0;
  }


  uint32_t meanAbs =
    (uint32_t)(
      sumAbs /
      used
    );


  // Mix average speech energy with some transient information.
  uint32_t activity =
    meanAbs +
    peak / 10;


  // Fast mouth opening, slower closing. This avoids nervous flicker
  // while keeping visible consonant/vowel movement responsive.
  if(
    activity >
    ttsMouthEnvelope
  ) {

    ttsMouthEnvelope =
      (
        ttsMouthEnvelope +
        activity * 3UL
      ) /
      4UL;
  }
  else {

    ttsMouthEnvelope =
      (
        ttsMouthEnvelope * 5UL +
        activity
      ) /
      6UL;
  }


  if(
    ttsMouthEnvelope < 420
  ) {
    return 0;
  }


  if(
    ttsMouthEnvelope < 1800
  ) {
    return 1;
  }


  if(
    ttsMouthEnvelope < 4800
  ) {
    return 2;
  }


  return 3;
}



bool queuePreparedPcm(
  const PreparedTtsChunk& chunk
) {

  if(
    chunk.pcm == nullptr ||
    chunk.pcmBytes == 0
  ) {
    return false;
  }


  static int16_t stereoOut[2048];

  uint32_t offset =
    0;


  if(
    chunk.channels == 1
  ) {

    while(
      offset + 1 <
      chunk.pcmBytes
    ) {

      size_t samples =
        (
          chunk.pcmBytes -
          offset
        ) /
        sizeof(int16_t);


      if(
        samples > 1024
      ) {
        samples =
          1024;
      }


      const int16_t* input =
        reinterpret_cast<const int16_t*>(
          chunk.pcm +
          offset
        );


      elliVisualAudioSpeechLevel(
        mouthLevelFromPcm(
          input,
          samples
        )
      );


      for(
        size_t i = 0;
        i < samples;
        i++
      ) {
        int16_t s =
          scalePcm(
            input[i]
          );

        stereoOut[i * 2] =
          s;

        stereoOut[i * 2 + 1] =
          s;
      }


      size_t outBytes =
        samples *
        2 *
        sizeof(int16_t);


      if(
        kiraAudioOutputWrite(
          reinterpret_cast<const uint8_t*>(
            stereoOut
          ),
          outBytes
        ) != outBytes
      ) {

        Serial.println(
          "[TTS/NEXT] Audio output queue write failed"
        );

        return false;
      }


      offset +=
        (uint32_t)(
          samples *
          sizeof(int16_t)
        );
    }
  }
  else if(
    chunk.channels == 2
  ) {

    while(
      offset + 3 <
      chunk.pcmBytes
    ) {

      size_t frames =
        (
          chunk.pcmBytes -
          offset
        ) /
        (
          2 *
          sizeof(int16_t)
        );


      if(
        frames > 1024
      ) {
        frames =
          1024;
      }


      const int16_t* input =
        reinterpret_cast<const int16_t*>(
          chunk.pcm +
          offset
        );


      elliVisualAudioSpeechLevel(
        mouthLevelFromPcm(
          input,
          frames * 2
        )
      );


      for(
        size_t i = 0;
        i < frames * 2;
        i++
      ) {
        stereoOut[i] =
          scalePcm(
            input[i]
          );
      }


      size_t outBytes =
        frames *
        2 *
        sizeof(int16_t);


      if(
        kiraAudioOutputWrite(
          reinterpret_cast<const uint8_t*>(
            stereoOut
          ),
          outBytes
        ) != outBytes
      ) {
        Serial.println(
          "[TTS/NEXT] Audio output queue write failed"
        );

        return false;
      }


      offset +=
        (uint32_t)outBytes;
    }
  }
  else {
    return false;
  }


  return true;
}


bool feedPreparedPcm(
  const PreparedTtsChunk& chunk
) {

  if(
    chunk.pcm == nullptr ||
    chunk.pcmBytes == 0
  ) {
    return false;
  }


  static int16_t stereoOut[2048];

  uint32_t offset =
    0;


  if(
    chunk.channels == 1
  ) {

    while(
      offset + 1 <
      chunk.pcmBytes
    ) {

      size_t samples =
        (
          chunk.pcmBytes -
          offset
        ) /
        sizeof(int16_t);


      if(
        samples > 1024
      ) {
        samples = 1024;
      }


      const int16_t* input =
        reinterpret_cast<const int16_t*>(
          chunk.pcm +
          offset
        );


      elliVisualAudioSpeechLevel(
        mouthLevelFromPcm(
          input,
          samples
        )
      );


      for(
        size_t i = 0;
        i < samples;
        i++
      ) {

        int16_t s =
          scalePcm(
            input[i]
          );

        stereoOut[
          i * 2
        ] = s;

        stereoOut[
          i * 2 + 1
        ] = s;
      }


      size_t outBytes =
        samples *
        2 *
        sizeof(int16_t);


      if(
        !writeSpeakerAll(
          reinterpret_cast<const uint8_t*>(
            stereoOut
          ),
          outBytes
        )
      ) {
        return false;
      }


      offset +=
        (uint32_t)(
          samples *
          sizeof(int16_t)
        );
    }
  }
  else {

    while(
      offset + 3 <
      chunk.pcmBytes
    ) {

      size_t frames =
        (
          chunk.pcmBytes -
          offset
        ) /
        (
          2 *
          sizeof(int16_t)
        );


      if(
        frames > 1024
      ) {
        frames = 1024;
      }


      const int16_t* input =
        reinterpret_cast<const int16_t*>(
          chunk.pcm +
          offset
        );



      elliVisualAudioSpeechLevel(
        mouthLevelFromPcm(
          input,
          frames * 2
        )
      );


      for(
        size_t i = 0;
        i < frames * 2;
        i++
      ) {

        stereoOut[i] =
          scalePcm(
            input[i]
          );
      }


      size_t outBytes =
        frames *
        2 *
        sizeof(int16_t);


      if(
        !writeSpeakerAll(
          reinterpret_cast<const uint8_t*>(
            stereoOut
          ),
          outBytes
        )
      ) {
        return false;
      }


      offset +=
        (uint32_t)outBytes;
    }
  }


  return true;
}


bool playPreparedTtsChunks(
  PreparedTtsChunk* chunks,
  uint8_t count
) {

  if(
    chunks == nullptr ||
    count == 0
  ) {
    return false;
  }


  uint32_t sampleRate =
    chunks[0].sampleRate;

  uint16_t bits =
    chunks[0].bitsPerSample;


  if(
    bits != 16
  ) {
    return false;
  }


  // Trim only INTERNAL chunk edges. Keep the real beginning and ending
  // of the complete Elli reply untouched.
  for(
    uint8_t i = 0;
    i < count;
    i++
  ) {

    trimInternalChunkSilence(
      chunks[i],
      i > 0,
      i + 1 < count
    );
  }


  if(
    !kiraVoiceTtsSpeakerConfigure(
      sampleRate
    )
  ) {

    Serial.println(
      "[TTS/PREFETCH] Speaker configure failed"
    );

    return false;
  }


  kiraVoiceTtsSpeakerEnable(
    true
  );

  delay(
    35
  );


  // Phase 5M: real audio now owns Elli's SPEAKING lifetime.
  ttsMouthEnvelope =
    0;

  elliVisualAudioSpeechBegin();


  bool ok =
    true;


  Serial.print(
    "[TTS/PREFETCH] Seamless playback chunks="
  );

  Serial.println(
    count
  );


  for(
    uint8_t i = 0;
    i < count;
    i++
  ) {

    if(
      chunks[i].sampleRate !=
      sampleRate ||
      chunks[i].bitsPerSample !=
      bits
    ) {

      // Extremely unlikely with one provider/model. If it happens,
      // reconfigure locally with no network delay and keep the amp up.
      if(
        !kiraVoiceTtsSpeakerConfigure(
          chunks[i].sampleRate
        )
      ) {
        ok = false;
        break;
      }


      sampleRate =
        chunks[i].sampleRate;

      bits =
        chunks[i].bitsPerSample;
    }


    if(
      !feedPreparedPcm(
        chunks[i]
      )
    ) {
      ok = false;
      break;
    }
  }


  elliVisualAudioSpeechLevel(
    0
  );


  kiraVoiceTtsSpeakerSilence();

  delay(
    25
  );

  kiraVoiceTtsSpeakerEnable(
    false
  );

  kiraVoiceTtsSpeakerRestore();


  elliVisualAudioSpeechEnd();


  return ok;
}

} // namespace


bool kiraTtsConfigured() {
  const char* key = kiraVoiceGroqApiKey();

  bool groqReady =
    key != nullptr &&
    strlen(key) > 20;

  return
    groqReady ||
    kiraTtsHybridConfigured();
}


bool kiraTtsSpeakGroqPrimary(const String& inputText) {

  groqSpokenChars =
    0;


  const char* groqKey =
    kiraVoiceGroqApiKey();


  if(
    groqKey == nullptr ||
    strlen(groqKey) <= 20
  ) {

    Serial.println(
      "[TTS/GROQ] Shared Groq credential unavailable"
    );

    return false;
  }


  if(
    WiFi.status() !=
    WL_CONNECTED
  ) {

    Serial.println(
      "[TTS/GROQ] Wi-Fi disconnected; online speech unavailable"
    );

    return false;
  }


  String text =
    normalizeForSpeech(
      inputText
    );


  if(
    !text.length()
  ) {
    return false;
  }


#if !KIRA_STREAMING_TTS_ENABLED
  Serial.println(
    "[TTS/NEXT] streaming output disabled"
  );

  return false;
#else

  Serial.println(
    "[TTS/NEXT V2.6] Complete-WAV TTS -> deep PSRAM queue -> dedicated playback task"
  );

  Serial.println(
    "[TTS/NEXT V2.7] Incomplete WAV rejection + provider-aware 429 cooldown"
  );

  Serial.println(
    "[TTS/NEXT V2.2] Full-answer mode: do not abandon later text on one transient request"
  );

  Serial.print(
    "[TTS/NEXT] Elli speech chars: "
  );

  Serial.println(
    text.length()
  );


  size_t cursor =
    0;

  uint16_t chunkIndex =
    0;

  bool audioStarted =
    false;

  uint32_t streamSampleRate =
    0;

  bool failed =
    false;


  ttsMouthEnvelope =
    0;


  while(
    cursor <
    text.length()
  ) {

    while(
      cursor < text.length() &&
      text[cursor] == ' '
    ) {
      cursor++;
    }


    if(
      cursor >=
      text.length()
    ) {
      break;
    }


    size_t chunkEnd =
      chooseChunkEnd(
        text,
        cursor
      );


    if(
      chunkEnd <= cursor
    ) {
      Serial.println(
        "[TTS/NEXT] Chunk splitter made no progress"
      );

      failed =
        true;

      break;
    }


    String chunkText =
      text.substring(
        cursor,
        chunkEnd
      );

    chunkText.trim();


    if(
      !chunkText.length()
    ) {
      cursor =
        chunkEnd;

      continue;
    }


    chunkIndex++;


    PreparedTtsChunk chunk;


    Serial.print(
      "[TTS/NEXT] Fetch chunk "
    );

    Serial.print(
      chunkIndex
    );

    Serial.print(
      " chars="
    );

    Serial.print(
      chunkText.length()
    );

    Serial.print(
      " buffered="
    );

    Serial.print(
      kiraAudioOutputBufferedMs()
    );

    Serial.println(
      "ms"
    );


    if(
      !requestPreparedTtsChunk(
        chunkText,
        chunkIndex,
        chunk
      )
    ) {

      Serial.print(
        "[TTS/NEXT] Chunk "
      );

      Serial.print(
        chunkIndex
      );

      Serial.println(
        " failed after 5 attempts; provider/fallback required"
      );

      freePreparedTtsChunk(
        chunk
      );

      failed =
        true;

      break;
    }


    if(
      !audioStarted
    ) {

      streamSampleRate =
        chunk.sampleRate;


      if(
        !kiraAudioOutputStart(
          streamSampleRate
        )
      ) {

        Serial.println(
          "[TTS/NEXT] Audio output session failed to start"
        );

        freePreparedTtsChunk(
          chunk
        );

        failed =
          true;

        break;
      }


      audioStarted =
        true;

      // Elli enters SPEAKING only after the first real audio chunk exists
      // and the output session has successfully started.
      elliVisualAudioSpeechBegin();
    }
    else if(
      chunk.sampleRate !=
      streamSampleRate
    ) {

      Serial.println(
        "[TTS/NEXT] Provider sample-rate changed mid-answer"
      );

      freePreparedTtsChunk(
        chunk
      );

      failed =
        true;

      break;
    }


    trimInternalChunkSilence(
      chunk,
      chunkIndex > 1,
      chunkEnd <
        text.length()
    );


    if(
      !queuePreparedPcm(
        chunk
      )
    ) {

      freePreparedTtsChunk(
        chunk
      );

      failed =
        true;

      break;
    }


    freePreparedTtsChunk(
      chunk
    );


    cursor =
      chunkEnd;

    groqSpokenChars =
      cursor;


    Serial.print(
      "[TTS/NEXT] queued progress="
    );

    Serial.print(
      groqSpokenChars
    );

    Serial.print(
      "/"
    );

    Serial.print(
      text.length()
    );

    Serial.print(
      " buffered="
    );

    Serial.print(
      kiraAudioOutputBufferedMs()
    );

    Serial.println(
      "ms"
    );


    // The deeper AudioOutput ring is the rate smoother now. Begin the next
    // request immediately so network synthesis stays ahead of playback.
    taskYIELD();
  }


  bool drainedOK =
    true;


  if(
    audioStarted
  ) {
    drainedOK =
      kiraAudioOutputFinish(
        45000
      );
  }


  elliVisualAudioSpeechLevel(
    0
  );

  elliVisualAudioSpeechEnd();


  if(
    failed ||
    !drainedOK
  ) {

    if(
      !drainedOK
    ) {
      kiraMetricsIncrement(
        KIRA_METRIC_TTS_FAILURES
      );
    }


    Serial.print(
      "[TTS/NEXT] Groq rolling stream stopped after chars="
    );

    Serial.print(
      groqSpokenChars
    );

    Serial.print(
      "/"
    );

    Serial.println(
      text.length()
    );


    return false;
  }


  groqSpokenChars =
    text.length();


  Serial.print(
    "[TTS/NEXT] COMPLETE | chunks="
  );

  Serial.print(
    chunkIndex
  );

  Serial.print(
    " | underruns="
  );

  Serial.println(
    kiraAudioOutputUnderruns()
  );


  return true;
#endif
}
// ============================================================
// KIRA HYBRID TTS V1 - PUBLIC PROVIDER CHAIN
// ============================================================

bool kiraTtsSpeak(
  const String& inputText
) {

  String speechText =
    normalizeForSpeech(
      inputText
    );


  if(
    !speechText.length()
  ) {
    return false;
  }


  Serial.println();
  Serial.println(
    "[TTS/HYBRID] Chain: Groq Autumn -> Edge Emma -> PicoTTS -> beep"
  );


  groqSpokenChars =
    0;


  if(
    WiFi.status() == WL_CONNECTED
  ) {

    Serial.println(
      "[TTS/HYBRID] Trying primary Groq Autumn"
    );


    if(
      kiraTtsSpeakGroqPrimary(
        speechText
      )
    ) {

      Serial.println(
        "[TTS/HYBRID] Selected: GROQ_AUTUMN"
      );

      return true;
    }


    Serial.print(
      "[TTS/HYBRID] Groq stopped after chars: "
    );

    Serial.print(
      groqSpokenChars
    );

    Serial.print(
      "/"
    );

    Serial.println(
      speechText.length()
    );
  }
  else {

    Serial.println(
      "[TTS/HYBRID] Wi-Fi disconnected; skipping online primary"
    );
  }


  size_t remainingStart =
    groqSpokenChars;


  if(
    remainingStart > speechText.length()
  ) {
    remainingStart =
      speechText.length();
  }


  String remaining =
    speechText.substring(
      remainingStart
    );

  remaining.trim();


  if(
    !remaining.length()
  ) {

    // Groq may have completed playback and then reported a transport-state
    // failure during cleanup. Do not repeat an already-spoken answer.
    Serial.println(
      "[TTS/HYBRID] No unspoken text remains"
    );

    return true;
  }


  Serial.print(
    "[TTS/HYBRID] Backup receives ONLY remaining chars: "
  );

  Serial.println(
    remaining.length()
  );


  if(
    kiraTtsHybridFallback(
      remaining
    )
  ) {

    Serial.println(
      "[TTS/HYBRID] Remaining answer completed by backup provider"
    );

    return true;
  }


  Serial.println(
    "[TTS/HYBRID] No backup available for remaining answer"
  );

  Serial.println(
    "[TTS/HYBRID] Full answer remains visible on display/Serial"
  );


  return false;
}

// ============================================================
// GLOBAL ELLI TTS QUEUE API
// ============================================================

void kiraTtsQueue(
  const String& inputText
) {

  String text =
    inputText;

  text.trim();


  if(
    !text.length()
  ) {
    return;
  }


  if(
    ttsPendingItems >=
    TTS_PENDING_MAX
  ) {

    uint8_t last =
      (uint8_t)(
        (ttsPendingTail + TTS_PENDING_MAX - 1) %
        TTS_PENDING_MAX
      );

    if(
      ttsPending[last].length()
    ) {
      ttsPending[last] += " ";
    }

    ttsPending[last] += text;

    Serial.println(
      "[TTS/QUEUE] Full - coalesced newest Elli response"
    );

    return;
  }


  ttsPending[
    ttsPendingTail
  ] = text;

  ttsPendingTail =
    (uint8_t)(
      (ttsPendingTail + 1) %
      TTS_PENDING_MAX
    );

  ttsPendingItems++;


  Serial.print(
    "[TTS/QUEUE] Elli response queued. pending="
  );

  Serial.println(
    ttsPendingItems
  );
}


uint8_t kiraTtsPendingCount() {

  return
    ttsPendingItems;
}


bool kiraTtsFlushPending() {

  if(
    ttsPendingItems == 0
  ) {
    return false;
  }


  bool pausedByTts =
    kiraVoicePauseForTts();


  // ==========================================================
  // PHASE 5L.2B - NATURAL RESPONSE COALESCING
  // ==========================================================
  //
  // One Universal Brain turn can call elliSay() more than once.
  // Previously each queued response became a completely separate
  // Groq request/playback cycle, producing an obvious stop/restart.
  //
  // Combine everything currently pending into one natural utterance.
  // Long combined text is still sentence-chunked by kiraTtsSpeak().
  // ==========================================================

  String combined;
  combined.reserve(
    256
  );

  uint8_t mergedCount = 0;


  while(
    ttsPendingItems > 0
  ) {

    String part =
      ttsPending[
        ttsPendingHead
      ];

    ttsPending[
      ttsPendingHead
    ] = "";

    ttsPendingHead =
      (uint8_t)(
        (ttsPendingHead + 1) %
        TTS_PENDING_MAX
      );

    ttsPendingItems--;


    part.trim();

    if(
      !part.length()
    ) {
      continue;
    }


    if(
      combined.length()
    ) {

      char last =
        combined[
          combined.length() - 1
        ];

      if(
        last != '.' &&
        last != '!' &&
        last != '?' &&
        last != ':' &&
        last != ';' &&
        last != ','
      ) {
        combined += '.';
      }

      combined += ' ';
    }


    combined +=
      part;

    mergedCount++;
  }


  bool spoken = false;


  if(
    combined.length()
  ) {

    Serial.print(
      "[TTS/QUEUE] Merged Elli replies: "
    );

    Serial.print(
      mergedCount
    );

    Serial.print(
      " | chars="
    );

    Serial.println(
      combined.length()
    );


    spoken =
      kiraTtsSpeak(
        combined
      );


    if(
      !spoken
    ) {

      Serial.println(
        "[TTS/QUEUE] Merged Elli response could not be spoken"
      );
    }
  }


  kiraVoiceResumeAfterTts(
    pausedByTts
  );


  return
    spoken;
}

