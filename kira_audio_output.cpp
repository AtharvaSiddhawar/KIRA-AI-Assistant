#include "kira_audio_output.h"

#include "kira_metrics.h"
#include "kira_next_config.h"

#include <esp_heap_caps.h>

// Proven low-level speaker bridge remains in kira_voice.cpp during this
// transition stage. Only this AudioOutput task performs sustained TTS writes.
bool kiraVoiceTtsSpeakerConfigure(uint32_t sampleRate);
size_t kiraVoiceTtsSpeakerWrite(const uint8_t* data, size_t bytes);
void kiraVoiceTtsSpeakerEnable(bool enabled);
void kiraVoiceTtsSpeakerSilence();
void kiraVoiceTtsSpeakerRestore();

namespace {

// KIRA Group 1 TTS Continuity V2.2
//
// 24 kHz stereo/16-bit = ~96 KB/s.
// 1536 KB gives roughly 16 seconds of audio headroom. This is large enough
// for most 195-character Orpheus chunks to be queued quickly so the network
// producer can begin fetching the NEXT chunk while the current one speaks.
constexpr size_t AUDIO_OUT_RING_BYTES =
  1536UL * 1024UL;

// Give the network producer time to get ahead before the first syllable.
// Short replies still start as soon as producerFinished is set.
constexpr uint32_t AUDIO_OUT_PREBUFFER_MS =
  1800;

constexpr size_t AUDIO_OUT_TASK_CHUNK =
  4096;

constexpr uint32_t AUDIO_OUT_TASK_STACK =
  6144;

constexpr UBaseType_t AUDIO_OUT_TASK_PRIORITY =
  4;

uint8_t* ringBuffer =
  nullptr;

size_t ringRead =
  0;

size_t ringWrite =
  0;

size_t ringUsed =
  0;

SemaphoreHandle_t ringMutex =
  nullptr;

TaskHandle_t outputTaskHandle =
  nullptr;

volatile bool serviceReady =
  false;

volatile bool sessionActive =
  false;

volatile bool producerFinished =
  false;

volatile bool playbackStarted =
  false;

volatile bool abortRequested =
  false;

volatile bool drained =
  true;

volatile uint32_t currentSampleRate =
  0;

volatile uint32_t underrunCount =
  0;

bool underrunEpisode =
  false;

// After a real starvation event, do not repeatedly start/stop on a handful
// of arriving bytes. Refill a useful amount before continuing.
bool refillAfterUnderrun =
  false;

constexpr uint32_t AUDIO_OUT_REFILL_MS =
  700;


size_t bytesPerSecond() {
  uint32_t rate =
    currentSampleRate;

  if(
    rate < 8000 ||
    rate > 48000
  ) {
    return 0;
  }

  // TTS output sent to the speaker bridge is always stereo 16-bit.
  return
    (size_t)rate *
    2UL *
    sizeof(int16_t);
}


size_t prebufferBytes() {
  size_t bps =
    bytesPerSecond();

  if(
    bps == 0
  ) {
    return 0;
  }

  size_t wanted =
    bps *
    AUDIO_OUT_PREBUFFER_MS /
    1000UL;

  if(
    wanted >
    AUDIO_OUT_RING_BYTES / 2
  ) {
    wanted =
      AUDIO_OUT_RING_BYTES / 2;
  }

  if(
    wanted < 8192
  ) {
    wanted =
      8192;
  }

  return wanted;
}


void resetRingLocked() {
  ringRead = 0;
  ringWrite = 0;
  ringUsed = 0;
}


bool writeI2sAll(
  const uint8_t* data,
  size_t bytes
) {
  if(
    data == nullptr ||
    bytes == 0
  ) {
    return true;
  }

  size_t sent =
    0;

  uint32_t lastProgress =
    millis();

  while(
    sent < bytes &&
    !abortRequested
  ) {
    size_t n =
      kiraVoiceTtsSpeakerWrite(
        data + sent,
        bytes - sent
      );

    if(
      n > 0
    ) {
      sent += n;
      lastProgress = millis();
      continue;
    }

    if(
      millis() - lastProgress >
      1500
    ) {
      Serial.println(
        "[AUDIO OUT] I2S_WRITE_STALL"
      );

      return false;
    }

    vTaskDelay(
      pdMS_TO_TICKS(1)
    );
  }

  return
    !abortRequested &&
    sent == bytes;
}


void outputTask(
  void*
) {
  static uint8_t staging[
    AUDIO_OUT_TASK_CHUNK
  ];

  for(;;) {

    if(
      !sessionActive
    ) {
      vTaskDelay(
        pdMS_TO_TICKS(2)
      );
      continue;
    }


    size_t available =
      0;

    bool finished =
      false;

    if(
      ringMutex != nullptr &&
      xSemaphoreTake(
        ringMutex,
        pdMS_TO_TICKS(10)
      ) == pdTRUE
    ) {
      available =
        ringUsed;

      finished =
        producerFinished;

      xSemaphoreGive(
        ringMutex
      );
    }


    if(
      !playbackStarted ||
      refillAfterUnderrun
    ) {
      size_t startBytes =
        prebufferBytes();

      if(
        refillAfterUnderrun
      ) {
        size_t bps =
          bytesPerSecond();

        startBytes =
          bps *
          AUDIO_OUT_REFILL_MS /
          1000UL;

        if(
          startBytes < 8192
        ) {
          startBytes =
            8192;
        }
      }

      if(
        available >= startBytes ||
        (
          finished &&
          available > 0
        )
      ) {
        playbackStarted =
          true;

        refillAfterUnderrun =
          false;

        drained =
          false;

        underrunEpisode =
          false;

        Serial.print(
          "[AUDIO OUT] PLAYBACK_START buffered="
        );

        Serial.print(
          kiraAudioOutputBufferedMs()
        );

        Serial.println(
          "ms"
        );
      }
      else if(
        finished &&
        available == 0
      ) {
        drained =
          true;

        vTaskDelay(
          pdMS_TO_TICKS(1)
        );

        continue;
      }
      else {
        vTaskDelay(
          pdMS_TO_TICKS(2)
        );

        continue;
      }
    }


    size_t take =
      0;

    if(
      ringMutex != nullptr &&
      xSemaphoreTake(
        ringMutex,
        pdMS_TO_TICKS(10)
      ) == pdTRUE
    ) {

      take =
        ringUsed;

      if(
        take >
        sizeof(staging)
      ) {
        take =
          sizeof(staging);
      }


      if(
        take > 0
      ) {
        size_t first =
          AUDIO_OUT_RING_BYTES -
          ringRead;

        if(
          first >
          take
        ) {
          first =
            take;
        }

        memcpy(
          staging,
          ringBuffer + ringRead,
          first
        );

        size_t second =
          take -
          first;

        if(
          second > 0
        ) {
          memcpy(
            staging + first,
            ringBuffer,
            second
          );
        }

        ringRead =
          (
            ringRead +
            take
          ) %
          AUDIO_OUT_RING_BYTES;

        ringUsed -=
          take;
      }

      finished =
        producerFinished;

      xSemaphoreGive(
        ringMutex
      );
    }


    if(
      take > 0
    ) {
      underrunEpisode =
        false;

      if(
        !writeI2sAll(
          staging,
          take
        )
      ) {
        Serial.println(
          "[AUDIO OUT] PLAYBACK_WRITE_FAILED"
        );

        kiraMetricsIncrement(
          KIRA_METRIC_AUDIO_OUTPUT_UNDERRUNS
        );

        abortRequested =
          true;

        drained =
          true;

        continue;
      }

      continue;
    }


    if(
      finished
    ) {
      drained =
        true;

      playbackStarted =
        false;

      continue;
    }


    // A real starvation only exists after playback has already begun.
    if(
      playbackStarted &&
      !underrunEpisode
    ) {
      underrunEpisode =
        true;

      underrunCount++;

      kiraMetricsIncrement(
        KIRA_METRIC_AUDIO_OUTPUT_UNDERRUNS
      );

      Serial.println(
        "[AUDIO OUT] UNDERRUN - refilling before playback resumes"
      );

      playbackStarted =
        false;

      refillAfterUnderrun =
        true;
    }


    vTaskDelay(
      pdMS_TO_TICKS(2)
    );
  }
}

} // namespace


bool kiraAudioOutputBegin() {
#if !KIRA_STREAMING_TTS_ENABLED
  return true;
#else
  if(
    serviceReady
  ) {
    return true;
  }


  ringMutex =
    xSemaphoreCreateMutex();

  if(
    ringMutex == nullptr
  ) {
    Serial.println(
      "[AUDIO OUT] mutex allocation failed"
    );

    return false;
  }


  ringBuffer =
    static_cast<uint8_t*>(
      heap_caps_malloc(
        AUDIO_OUT_RING_BYTES,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      )
    );

  if(
    ringBuffer == nullptr
  ) {
    Serial.println(
      "[AUDIO OUT] PSRAM ring allocation failed"
    );

    vSemaphoreDelete(
      ringMutex
    );

    ringMutex =
      nullptr;

    return false;
  }


  BaseType_t taskOK =
    xTaskCreatePinnedToCore(
      outputTask,
      "KIRA_AudioOut",
      AUDIO_OUT_TASK_STACK,
      nullptr,
      AUDIO_OUT_TASK_PRIORITY,
      &outputTaskHandle,
      1
    );


  if(
    taskOK != pdPASS
  ) {
    Serial.println(
      "[AUDIO OUT] task creation failed"
    );

    heap_caps_free(
      ringBuffer
    );

    ringBuffer =
      nullptr;

    vSemaphoreDelete(
      ringMutex
    );

    ringMutex =
      nullptr;

    return false;
  }


  serviceReady =
    true;


  Serial.print(
    "[AUDIO OUT] READY | PSRAM ring="
  );

  Serial.print(
    AUDIO_OUT_RING_BYTES / 1024UL
  );

  Serial.println(
    " KB | task=Core1/P4"
  );

  Serial.println(
    "[AUDIO OUT] Continuity V2.2 | prebuffer=1800ms | underrun refill=700ms"
  );


  return true;
#endif
}


bool kiraAudioOutputStart(
  uint32_t sampleRate
) {
#if !KIRA_STREAMING_TTS_ENABLED
  (void)sampleRate;
  return false;
#else
  if(
    sampleRate < 8000 ||
    sampleRate > 48000
  ) {
    return false;
  }


  if(
    !kiraAudioOutputBegin()
  ) {
    return false;
  }


  kiraAudioOutputAbort();


  if(
    !kiraVoiceTtsSpeakerConfigure(
      sampleRate
    )
  ) {
    Serial.println(
      "[AUDIO OUT] speaker configure failed"
    );

    return false;
  }


  if(
    xSemaphoreTake(
      ringMutex,
      pdMS_TO_TICKS(50)
    ) != pdTRUE
  ) {
    return false;
  }


  resetRingLocked();

  currentSampleRate =
    sampleRate;

  producerFinished =
    false;

  playbackStarted =
    false;

  abortRequested =
    false;

  drained =
    false;

  sessionActive =
    true;

  underrunEpisode =
    false;

  refillAfterUnderrun =
    false;


  xSemaphoreGive(
    ringMutex
  );


  kiraVoiceTtsSpeakerEnable(
    true
  );

  delay(
    20
  );


  Serial.print(
    "[AUDIO OUT] SESSION_START rate="
  );

  Serial.print(
    sampleRate
  );

  Serial.print(
    "Hz prebuffer="
  );

  Serial.print(
    AUDIO_OUT_PREBUFFER_MS
  );

  Serial.println(
    "ms"
  );


  return true;
#endif
}


size_t kiraAudioOutputWrite(
  const uint8_t* data,
  size_t bytes,
  uint32_t stallTimeoutMs
) {
#if !KIRA_STREAMING_TTS_ENABLED
  (void)data;
  (void)bytes;
  (void)stallTimeoutMs;
  return 0;
#else
  if(
    data == nullptr ||
    bytes == 0 ||
    !sessionActive ||
    ringBuffer == nullptr ||
    ringMutex == nullptr
  ) {
    return 0;
  }


  size_t written =
    0;

  uint32_t lastProgress =
    millis();


  while(
    written < bytes &&
    sessionActive &&
    !abortRequested
  ) {
    size_t copied =
      0;


    if(
      xSemaphoreTake(
        ringMutex,
        pdMS_TO_TICKS(20)
      ) == pdTRUE
    ) {
      size_t room =
        AUDIO_OUT_RING_BYTES -
        ringUsed;

      copied =
        bytes -
        written;

      if(
        copied >
        room
      ) {
        copied =
          room;
      }


      if(
        copied > 0
      ) {
        size_t first =
          AUDIO_OUT_RING_BYTES -
          ringWrite;

        if(
          first >
          copied
        ) {
          first =
            copied;
        }


        memcpy(
          ringBuffer + ringWrite,
          data + written,
          first
        );


        size_t second =
          copied -
          first;

        if(
          second > 0
        ) {
          memcpy(
            ringBuffer,
            data + written + first,
            second
          );
        }


        ringWrite =
          (
            ringWrite +
            copied
          ) %
          AUDIO_OUT_RING_BYTES;

        ringUsed +=
          copied;
      }


      xSemaphoreGive(
        ringMutex
      );
    }


    if(
      copied > 0
    ) {
      written +=
        copied;

      lastProgress =
        millis();

      continue;
    }


    if(
      millis() - lastProgress >
      stallTimeoutMs
    ) {
      Serial.println(
        "[AUDIO OUT] QUEUE_STALL"
      );

      break;
    }


    delay(
      1
    );
  }


  return written;
#endif
}


bool kiraAudioOutputFinish(
  uint32_t drainTimeoutMs
) {
#if !KIRA_STREAMING_TTS_ENABLED
  (void)drainTimeoutMs;
  return false;
#else
  if(
    !sessionActive
  ) {
    return true;
  }


  producerFinished =
    true;


  Serial.print(
    "[AUDIO OUT] PRODUCER_DONE buffered="
  );

  Serial.print(
    kiraAudioOutputBufferedMs()
  );

  Serial.println(
    "ms"
  );


  uint32_t start =
    millis();


  while(
    !drained &&
    !abortRequested
  ) {
    if(
      millis() - start >
      drainTimeoutMs
    ) {
      Serial.println(
        "[AUDIO OUT] DRAIN_TIMEOUT"
      );

      kiraMetricsIncrement(
        KIRA_METRIC_AUDIO_OUTPUT_UNDERRUNS
      );

      kiraAudioOutputAbort();

      return false;
    }

    delay(
      2
    );
  }


  bool clean =
    !abortRequested;


  kiraVoiceTtsSpeakerSilence();

  delay(
    20
  );

  kiraVoiceTtsSpeakerEnable(
    false
  );

  kiraVoiceTtsSpeakerRestore();


  sessionActive =
    false;

  producerFinished =
    false;

  playbackStarted =
    false;

  abortRequested =
    false;

  drained =
    true;

  currentSampleRate =
    0;


  Serial.print(
    "[AUDIO OUT] SESSION_END underruns="
  );

  Serial.println(
    underrunCount
  );


  return clean;
#endif
}


void kiraAudioOutputAbort() {
#if KIRA_STREAMING_TTS_ENABLED
  abortRequested =
    true;

  producerFinished =
    true;


  if(
    ringMutex != nullptr &&
    xSemaphoreTake(
      ringMutex,
      pdMS_TO_TICKS(50)
    ) == pdTRUE
  ) {
    resetRingLocked();

    xSemaphoreGive(
      ringMutex
    );
  }


  if(
    sessionActive
  ) {
    kiraVoiceTtsSpeakerSilence();

    kiraVoiceTtsSpeakerEnable(
      false
    );
  }


  sessionActive =
    false;

  playbackStarted =
    false;

  producerFinished =
    false;

  drained =
    true;

  currentSampleRate =
    0;

  abortRequested =
    false;
#endif
}


uint32_t kiraAudioOutputBufferedBytes() {
#if !KIRA_STREAMING_TTS_ENABLED
  return 0;
#else
  uint32_t used =
    0;

  if(
    ringMutex != nullptr &&
    xSemaphoreTake(
      ringMutex,
      pdMS_TO_TICKS(10)
    ) == pdTRUE
  ) {
    used =
      (uint32_t)ringUsed;

    xSemaphoreGive(
      ringMutex
    );
  }

  return used;
#endif
}


uint32_t kiraAudioOutputBufferedMs() {
#if !KIRA_STREAMING_TTS_ENABLED
  return 0;
#else
  size_t bps =
    bytesPerSecond();

  if(
    bps == 0
  ) {
    return 0;
  }

  return
    (uint32_t)(
      (
        (uint64_t)kiraAudioOutputBufferedBytes() *
        1000ULL
      ) /
      bps
    );
#endif
}


uint32_t kiraAudioOutputUnderruns() {
  return
    underrunCount;
}


bool kiraAudioOutputActive() {
  return
    sessionActive;
}
