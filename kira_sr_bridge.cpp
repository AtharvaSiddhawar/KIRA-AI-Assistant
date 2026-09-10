#include "kira_sr_bridge.h"
#include "kira_audio_engine.h"
#include "kira_next_config.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr uint32_t KIRA_SR_SAMPLE_RATE = 16000;
constexpr uint32_t KIRA_SR_CAPTURE_MS = 15000;
constexpr uint32_t KIRA_SR_PREROLL_MS = 320;

constexpr size_t KIRA_SR_CAPTURE_SAMPLES =
  (size_t)KIRA_SR_SAMPLE_RATE * KIRA_SR_CAPTURE_MS / 1000UL;

constexpr size_t KIRA_SR_PREROLL_SAMPLES =
  (size_t)KIRA_SR_SAMPLE_RATE * KIRA_SR_PREROLL_MS / 1000UL;

uint32_t meanAbsBridge(
  const int16_t* samples,
  size_t count
) {
  if (!samples || count == 0) return 0;

  uint64_t sum = 0;

  for (size_t i = 0; i < count; i++) {
    int32_t v = samples[i];
    if (v < 0) v = -v;
    sum += (uint32_t)v;
  }

  return (uint32_t)(sum / count);
}

}

KiraSRBridge KIRA_SR;

KiraSRBridge::KiraSRBridge()
  : mic_(nullptr),
    user_cb_(nullptr),
    capture_mutex_(nullptr),
    capture_buffer_(nullptr),
    capture_capacity_samples_(0),
    capture_samples_(0),
    preroll_buffer_(nullptr),
    preroll_capacity_samples_(0),
    preroll_write_(0),
    preroll_count_(0),
    capture_active_(false),
    capture_ready_(false),
    capture_overflowed_(false),
    capture_speech_seen_(false),
    capture_last_active_sample_(0),
    capture_noise_floor_(0xFFFFFFFFUL),
    started_(false) {}

KiraSRBridge::~KiraSRBridge() {
  end();
  freeCaptureMemory();

  if (capture_mutex_ != nullptr) {
    vSemaphoreDelete(capture_mutex_);
    capture_mutex_ = nullptr;
  }
}

void KiraSRBridge::onEvent(kira_sr_cb cb) {
  user_cb_ = cb;
}

bool KiraSRBridge::allocateCaptureMemory() {
  if (capture_mutex_ == nullptr) {
    capture_mutex_ = xSemaphoreCreateMutex();
    if (capture_mutex_ == nullptr) {
      return false;
    }
  }

  if (capture_buffer_ == nullptr) {
    capture_buffer_ = (int16_t*)heap_caps_malloc(
      KIRA_SR_CAPTURE_SAMPLES * sizeof(int16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
  }

  if (preroll_buffer_ == nullptr) {
    preroll_buffer_ = (int16_t*)heap_caps_malloc(
      KIRA_SR_PREROLL_SAMPLES * sizeof(int16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
  }

  if (capture_buffer_ == nullptr || preroll_buffer_ == nullptr) {
    freeCaptureMemory();
    return false;
  }

  capture_capacity_samples_ = KIRA_SR_CAPTURE_SAMPLES;
  preroll_capacity_samples_ = KIRA_SR_PREROLL_SAMPLES;
  capture_samples_ = 0;
  preroll_write_ = 0;
  preroll_count_ = 0;
  capture_active_ = false;
  capture_ready_ = false;
  capture_overflowed_ = false;
  capture_speech_seen_ = false;
  capture_last_active_sample_ = 0;
  capture_noise_floor_ = 0xFFFFFFFFUL;

  return true;
}

void KiraSRBridge::freeCaptureMemory() {
  if (capture_buffer_ != nullptr) {
    heap_caps_free(capture_buffer_);
    capture_buffer_ = nullptr;
  }

  if (preroll_buffer_ != nullptr) {
    heap_caps_free(preroll_buffer_);
    preroll_buffer_ = nullptr;
  }

  capture_capacity_samples_ = 0;
  preroll_capacity_samples_ = 0;
  capture_samples_ = 0;
  preroll_write_ = 0;
  preroll_count_ = 0;
  capture_active_ = false;
  capture_ready_ = false;
  capture_overflowed_ = false;
  capture_speech_seen_ = false;
  capture_last_active_sample_ = 0;
  capture_noise_floor_ = 0xFFFFFFFFUL;
}

bool KiraSRBridge::begin(
  I2SClass& i2s,
  const sr_cmd_t* commands,
  size_t command_count,
  sr_channels_t rx_chan,
  sr_mode_t mode,
  const char* input_format
) {
  if (started_) {
    return true;
  }

  if (!allocateCaptureMemory()) {
    Serial.println("[VOICE/ROUTER] ERROR: PSRAM capture bridge allocation failed");
    return false;
  }

  mic_ = &i2s;

  esp_err_t err = sr_start(
    &KiraSRBridge::fillThunk,
    this,
    rx_chan,
    mode,
    input_format,
    commands,
    command_count,
    &KiraSRBridge::eventThunk,
    this
  );

  started_ = (err == ESP_OK);

  if (started_) {
    Serial.println(
      "[AUDIO IN] SAFE DIRECT FEED | ESP-SR owns real-time I2S0 read | KIRA observes same PCM"
    );
    Serial.println(
      "[AUDIO IN] Independent preserved utterance capacity=15000ms"
    );
  }

  if (!started_) {
    Serial.print("[VOICE/ROUTER] ERROR: sr_start failed: ");
    Serial.println((int)err);
  }

  return started_;
}

bool KiraSRBridge::end() {
  if (!started_) {
    return true;
  }

  esp_err_t err = sr_stop();
  started_ = false;
  mic_ = nullptr;
  return err == ESP_OK;
}

bool KiraSRBridge::setMode(sr_mode_t mode) {
  return started_ && sr_set_mode(mode) == ESP_OK;
}

bool KiraSRBridge::pause() {
  return started_ && sr_pause() == ESP_OK;
}

bool KiraSRBridge::resume() {
  return started_ && sr_resume() == ESP_OK;
}

esp_err_t KiraSRBridge::fillThunk(
  void* arg,
  void* out,
  size_t len,
  size_t* bytes_read,
  uint32_t timeout_ms
) {
  return static_cast<KiraSRBridge*>(arg)->fill(
    out,
    len,
    bytes_read,
    timeout_ms
  );
}

void KiraSRBridge::eventThunk(
  void* arg,
  sr_event_t event,
  int command_id,
  int phrase_id
) {
  static_cast<KiraSRBridge*>(arg)->handleEvent(
    event,
    command_id,
    phrase_id
  );
}

esp_err_t KiraSRBridge::fill(
  void* out,
  size_t len,
  size_t* bytes_read,
  uint32_t timeout_ms
) {
  if (bytes_read == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  *bytes_read = 0;

  if (mic_ == nullptr || out == nullptr || len == 0) {
    return ESP_FAIL;
  }

  mic_->setTimeout(timeout_ms);
  *bytes_read = mic_->readBytes((char*)out, len);

  if (*bytes_read >= sizeof(int16_t)) {
    const int16_t* input = (const int16_t*)out;
    size_t count = *bytes_read / sizeof(int16_t);

#if KIRA_AUDIO_ENGINE_ENABLED
    // KIRA Next: the same PCM that feeds ESP-SR is now also delivered to
    // the centralized 20 ms audio frontend. No second microphone read.
    kiraAudioEngineObserveInput(input,count);
#endif

    if (
      capture_mutex_ != nullptr &&
      xSemaphoreTake(capture_mutex_, 0) == pdTRUE
    ) {
      updatePreRoll(input, count);

      if (capture_active_ && capture_buffer_ != nullptr) {
        size_t free_samples =
          capture_capacity_samples_ > capture_samples_
            ? capture_capacity_samples_ - capture_samples_
            : 0;

        size_t to_copy = count < free_samples ? count : free_samples;

        if (to_copy > 0) {
          memcpy(
            capture_buffer_ + capture_samples_,
            input,
            to_copy * sizeof(int16_t)
          );
          capture_samples_ += to_copy;
        }

        if (to_copy < count) {
          capture_overflowed_ = true;
          capture_active_ = false;
        }

        // ----------------------------------------------------
        // Phase 5K.3 - live speech/end tracking
        // ----------------------------------------------------
        // This runs on the same microphone block that feeds ESP-SR.
        // We do not use a fixed number of seconds as "question done".
        // ----------------------------------------------------

        uint32_t level =
          meanAbsBridge(
            input,
            count
          );

        // Quiet blocks continuously improve our ambient-floor estimate.
        // Do not let obvious speech raise the floor.
        if (
          level < 900 &&
          (
            capture_noise_floor_ == 0xFFFFFFFFUL ||
            level < capture_noise_floor_
          )
        ) {
          capture_noise_floor_ = level;
        }

        uint32_t noiseFloor =
          capture_noise_floor_ == 0xFFFFFFFFUL
            ? 80UL
            : capture_noise_floor_;

        uint32_t speechThreshold =
          noiseFloor * 2UL + 120UL;

        if (speechThreshold < 320UL) speechThreshold = 320UL;
        if (speechThreshold > 5000UL) speechThreshold = 5000UL;

        uint32_t continuationThreshold =
          noiseFloor + noiseFloor / 2UL + 80UL;

        if (continuationThreshold < 180UL) {
          continuationThreshold = 180UL;
        }

        if (continuationThreshold > speechThreshold) {
          continuationThreshold = speechThreshold;
        }

        if (!capture_speech_seen_) {
          if (level >= speechThreshold) {
            capture_speech_seen_ = true;
            capture_last_active_sample_ = capture_samples_;
          }
        }
        else if (level >= continuationThreshold) {
          capture_last_active_sample_ = capture_samples_;
        }
      }

      xSemaphoreGive(capture_mutex_);
    }
  }

  return (esp_err_t)mic_->lastError();
}

void KiraSRBridge::updatePreRoll(
  const int16_t* samples,
  size_t count
) {
  if (
    preroll_buffer_ == nullptr ||
    preroll_capacity_samples_ == 0 ||
    samples == nullptr ||
    count == 0
  ) {
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    preroll_buffer_[preroll_write_] = samples[i];
    preroll_write_++;

    if (preroll_write_ >= preroll_capacity_samples_) {
      preroll_write_ = 0;
    }

    if (preroll_count_ < preroll_capacity_samples_) {
      preroll_count_++;
    }
  }
}

void KiraSRBridge::beginCommandCapture() {
  if (
    capture_mutex_ == nullptr ||
    capture_buffer_ == nullptr ||
    preroll_buffer_ == nullptr
  ) {
    return;
  }

  if (xSemaphoreTake(capture_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
    return;
  }

  capture_samples_ = 0;
  capture_ready_ = false;
  capture_overflowed_ = false;
  capture_speech_seen_ = false;

#if KIRA_AUDIO_ENGINE_ENABLED
  kiraAudioEngineStartUtterance();
#endif
  capture_last_active_sample_ = 0;
  capture_noise_floor_ = 0xFFFFFFFFUL;

  size_t available = preroll_count_;
  if (available > capture_capacity_samples_) {
    available = capture_capacity_samples_;
  }

  if (available > 0) {
    size_t start =
      (preroll_write_ + preroll_capacity_samples_ - available) %
      preroll_capacity_samples_;

    size_t first = preroll_capacity_samples_ - start;
    if (first > available) first = available;

    memcpy(
      capture_buffer_,
      preroll_buffer_ + start,
      first * sizeof(int16_t)
    );

    size_t second = available - first;
    if (second > 0) {
      memcpy(
        capture_buffer_ + first,
        preroll_buffer_,
        second * sizeof(int16_t)
      );
    }

    capture_samples_ = available;
  }

  capture_active_ = true;

  xSemaphoreGive(capture_mutex_);
}

void KiraSRBridge::finishCommandCapture(bool preserve) {
  if (capture_mutex_ == nullptr) {
    capture_active_ = false;
    capture_ready_ = false;
    return;
  }

  if (xSemaphoreTake(capture_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
    capture_active_ = false;
    capture_ready_ = false;
    return;
  }

  capture_active_ = false;
  capture_ready_ = preserve && capture_samples_ > 0;

  if (!preserve) {
    capture_samples_ = 0;
    capture_overflowed_ = false;
#if KIRA_AUDIO_ENGINE_ENABLED
    kiraAudioEngineCancelUtterance();
#endif
  }

  xSemaphoreGive(capture_mutex_);
}

void KiraSRBridge::handleEvent(
  sr_event_t event,
  int command_id,
  int phrase_id
) {
  if (
    (event == SR_EVENT_WAKEWORD ||
     event == SR_EVENT_WAKEWORD_CHANNEL) &&
    !capture_active_
  ) {
    beginCommandCapture();
  }
  else if (event == SR_EVENT_COMMAND) {
    finishCommandCapture(false);
  }
  else if (event == SR_EVENT_TIMEOUT) {
    // KIRA Next Group 1:
    // MultiNet timeout is NOT the end of the user's utterance.
    // ESP-SR's feed task keeps calling fill(), so the direct microphone
    // capture and VAD continue until the real speech-end watchdog preserves it.
  }

  if (user_cb_ != nullptr) {
    user_cb_(event, command_id, phrase_id);
  }
}

bool KiraSRBridge::capturedCommandAvailable() {
  return capture_ready_ && capture_samples_ > 0;
}

bool KiraSRBridge::preserveCurrentCommandCapture() {
  if (!started_ || capture_buffer_ == nullptr) {
    return false;
  }

  // The caller pauses ESP-SR first, so the capture buffer is stable
  // while we convert the active capture into a preserved capture.
  finishCommandCapture(true);

  return capturedCommandAvailable();
}

bool KiraSRBridge::commandCaptureSpeechSeen() {
#if KIRA_AUDIO_ENGINE_ENABLED && KIRA_VAD_V2_ENABLED
  return kiraAudioEngineSpeechSeen();
#else
  bool seen = false;
  if (capture_mutex_ == nullptr) return false;
  if (xSemaphoreTake(capture_mutex_,pdMS_TO_TICKS(5)) == pdTRUE) {
    seen = capture_speech_seen_;
    xSemaphoreGive(capture_mutex_);
  }
  return seen;
#endif
}

bool KiraSRBridge::commandCaptureSpeechEnded(
  uint32_t silence_ms
) {
#if KIRA_AUDIO_ENGINE_ENABLED && KIRA_VAD_V2_ENABLED
  return kiraAudioEngineSpeechEnded(silence_ms);
#else
  if (capture_mutex_ == nullptr || silence_ms == 0) return false;
  bool ended = false;
  if (xSemaphoreTake(capture_mutex_,pdMS_TO_TICKS(5)) == pdTRUE) {
    if (capture_active_ && capture_speech_seen_ && capture_samples_ >= capture_last_active_sample_) {
      size_t quietSamples=capture_samples_-capture_last_active_sample_;
      size_t requiredQuietSamples=(size_t)KIRA_SR_SAMPLE_RATE*silence_ms/1000UL;
      ended=quietSamples>=requiredQuietSamples;
    }
    xSemaphoreGive(capture_mutex_);
  }
  return ended;
#endif
}

bool KiraSRBridge::capturedCommandView(
  const int16_t*& pcm,
  size_t& samples
) {
  pcm = nullptr;
  samples = 0;

  if (!capturedCommandAvailable() || capture_buffer_ == nullptr) {
    return false;
  }

  pcm = capture_buffer_;
  samples = capture_samples_;
  return true;
}

void KiraSRBridge::discardCapturedCommand() {
#if KIRA_AUDIO_ENGINE_ENABLED
  kiraAudioEngineCancelUtterance();
#endif

  if (capture_mutex_ == nullptr) {
    capture_ready_ = false;
    capture_samples_ = 0;
    capture_overflowed_ = false;
    return;
  }

  if (xSemaphoreTake(capture_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    capture_ready_ = false;
    capture_samples_ = 0;
    capture_overflowed_ = false;
    xSemaphoreGive(capture_mutex_);
  }
}

bool KiraSRBridge::captureOverflowed() {
  return capture_overflowed_;
}
