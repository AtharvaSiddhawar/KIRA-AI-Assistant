#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include <ESP_SR.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================
// KIRA PHASE 5J-2 - ESP-SR CAPTURE BRIDGE
// ============================================================
//
// Uses Arduino-ESP32's public low-level ESP-SR API so the same
// microphone bytes that feed WakeNet/MultiNet can also be retained
// briefly in PSRAM after the wake word.
//
// This enables one-utterance fallback:
//   Hi ESP -> known MultiNet command -> local
//   Hi ESP -> unknown sentence       -> captured audio -> STT
//
// No Arduino core/library source files need to be edited.
// ============================================================

typedef void (*kira_sr_cb)(sr_event_t event, int command_id, int phrase_id);

class KiraSRBridge {
public:
  KiraSRBridge();
  ~KiraSRBridge();

  void onEvent(kira_sr_cb cb);

  bool begin(
    I2SClass& i2s,
    const sr_cmd_t* commands,
    size_t command_count,
    sr_channels_t rx_chan = SR_CHANNELS_STEREO,
    sr_mode_t mode = SR_MODE_WAKEWORD,
    const char* input_format = "MN"
  );

  bool end();
  bool setMode(sr_mode_t mode);
  bool pause();
  bool resume();

  bool capturedCommandAvailable();
  bool capturedCommandView(const int16_t*& pcm, size_t& samples);

  // Preserve the live command capture when KIRA has determined that
  // the user actually finished speaking.
  bool preserveCurrentCommandCapture();

  // Real speech-end detector for the currently active post-wake capture.
  // Returns true only after speech was seen and then remained below the
  // adaptive continuation threshold for at least silence_ms.
  bool commandCaptureSpeechEnded(
    uint32_t silence_ms
  );

  bool commandCaptureSpeechSeen();

  void discardCapturedCommand();
  bool captureOverflowed();

private:
  static esp_err_t fillThunk(
    void* arg,
    void* out,
    size_t len,
    size_t* bytes_read,
    uint32_t timeout_ms
  );

  static void eventThunk(
    void* arg,
    sr_event_t event,
    int command_id,
    int phrase_id
  );

  esp_err_t fill(
    void* out,
    size_t len,
    size_t* bytes_read,
    uint32_t timeout_ms
  );

  void handleEvent(
    sr_event_t event,
    int command_id,
    int phrase_id
  );

  bool allocateCaptureMemory();
  void freeCaptureMemory();
  void updatePreRoll(const int16_t* samples, size_t count);
  void beginCommandCapture();
  void finishCommandCapture(bool preserve);

  I2SClass* mic_;
  kira_sr_cb user_cb_;

  SemaphoreHandle_t capture_mutex_;

  int16_t* capture_buffer_;
  size_t capture_capacity_samples_;
  size_t capture_samples_;

  int16_t* preroll_buffer_;
  size_t preroll_capacity_samples_;
  size_t preroll_write_;
  size_t preroll_count_;

  volatile bool capture_active_;
  volatile bool capture_ready_;
  volatile bool capture_overflowed_;

  // Legacy fields retained for compatibility; KIRA Next VAD V2 is authoritative.
  bool capture_speech_seen_;
  size_t capture_last_active_sample_;
  uint32_t capture_noise_floor_;

  bool started_;
};

extern KiraSRBridge KIRA_SR;
