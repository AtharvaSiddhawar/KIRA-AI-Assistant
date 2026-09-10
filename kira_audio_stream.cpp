#include "kira_audio_stream.h"

#include "kira_audio_codec.h"
#include "kira_gateway.h"
#include "kira_next_config.h"
#include "kira_vad.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

namespace {

constexpr size_t PCM_FRAME_SAMPLES = 320;   // 20 ms @ 16 kHz
constexpr size_t NETWORK_FRAME_SAMPLES =
  (16000UL * KIRA_NETWORK_AUDIO_FRAME_MS) / 1000UL;

static_assert(
  NETWORK_FRAME_SAMPLES == 960,
  "Group B currently expects 60 ms / 960-sample Opus frames"
);

struct PcmFrame {
  int16_t pcm[PCM_FRAME_SAMPLES];
};

QueueHandle_t queueHandle = nullptr;

int16_t partial[PCM_FRAME_SAMPLES];
size_t partialCount = 0;

volatile bool ready = false;
volatile bool active = false;
volatile bool startRequested = false;
volatile bool finishRequested = false;
volatile bool cancelRequested = false;

volatile uint32_t droppedFrames = 0;
volatile uint32_t sentPackets = 0;
volatile uint32_t sentBytes = 0;

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

void clearQueue() {
  if (queueHandle) xQueueReset(queueHandle);
  portENTER_CRITICAL(&stateMux);
  partialCount = 0;
  portEXIT_CRITICAL(&stateMux);
}

void queueFrame(const int16_t* pcm) {
  if (!queueHandle || !pcm) return;

  PcmFrame frame;
  memcpy(frame.pcm, pcm, sizeof(frame.pcm));

  if (xQueueSend(queueHandle, &frame, 0) != pdTRUE) {
    droppedFrames++;
  }
}

} // namespace

bool kiraAudioStreamBegin() {
#if !KIRA_AUDIO_UPLINK_ENABLED
  return true;
#else
  if (ready) return true;

  queueHandle = xQueueCreate(
    KIRA_AUDIO_UPLINK_QUEUE_DEPTH,
    sizeof(PcmFrame)
  );

  if (!queueHandle) {
    Serial.println("[AUDIO STREAM] ERROR: uplink queue allocation failed");
    return false;
  }

  ready = true;

  Serial.print("[AUDIO STREAM] READY | queue=");
  Serial.print(KIRA_AUDIO_UPLINK_QUEUE_DEPTH);
  Serial.println(" x 20ms | network frame=60ms");

  return true;
#endif
}

void kiraAudioStreamStartTurn() {
#if KIRA_AUDIO_UPLINK_ENABLED
  if (!ready && !kiraAudioStreamBegin()) return;

  clearQueue();

  const bool canStream =
    kiraGatewayReady() &&
    kiraAudioCodecAvailable(KIRA_AUDIO_CODEC_OPUS);

  portENTER_CRITICAL(&stateMux);
  active = canStream;
  startRequested = canStream;
  finishRequested = false;
  cancelRequested = false;
  portEXIT_CRITICAL(&stateMux);

  if (!canStream && kiraGatewayEnabled()) {
    Serial.println("[AUDIO STREAM] bypassed: gateway handshake or Opus not ready");
  }
#endif
}

void kiraAudioStreamCancelTurn() {
#if KIRA_AUDIO_UPLINK_ENABLED
  portENTER_CRITICAL(&stateMux);
  active = false;
  cancelRequested = true;
  finishRequested = false;
  startRequested = false;
  partialCount = 0;
  portEXIT_CRITICAL(&stateMux);
#endif
}

void kiraAudioStreamObserveInput(const int16_t* samples, size_t count) {
#if KIRA_AUDIO_UPLINK_ENABLED
  if (!ready || !samples || count == 0) return;

  bool isActive;
  portENTER_CRITICAL(&stateMux);
  isActive = active;
  portEXIT_CRITICAL(&stateMux);

  if (!isActive) return;

  size_t offset = 0;

  while (offset < count) {
    size_t room;
    size_t take;

    portENTER_CRITICAL(&stateMux);
    room = PCM_FRAME_SAMPLES - partialCount;
    take = (count - offset < room) ? (count - offset) : room;

    memcpy(
      partial + partialCount,
      samples + offset,
      take * sizeof(int16_t)
    );

    partialCount += take;
    offset += take;

    const bool complete = partialCount == PCM_FRAME_SAMPLES;
    portEXIT_CRITICAL(&stateMux);

    if (complete) {
      queueFrame(partial);
      portENTER_CRITICAL(&stateMux);
      partialCount = 0;
      portEXIT_CRITICAL(&stateMux);
    }
  }

  if (kiraVadSpeechEnded(KIRA_AUDIO_UPLINK_END_SILENCE_MS)) {
    portENTER_CRITICAL(&stateMux);
    active = false;
    finishRequested = true;
    portEXIT_CRITICAL(&stateMux);
  }
#else
  (void)samples;
  (void)count;
#endif
}

void kiraAudioStreamService() {
#if KIRA_AUDIO_UPLINK_ENABLED
  if (!ready) return;

  bool doStart;
  bool doFinish;
  bool doCancel;

  portENTER_CRITICAL(&stateMux);
  doStart = startRequested;
  doFinish = finishRequested;
  doCancel = cancelRequested;
  startRequested = false;
  cancelRequested = false;
  portEXIT_CRITICAL(&stateMux);

  if (doCancel) {
    clearQueue();
    if (kiraGatewayReady()) kiraGatewayAbort("audio_cancel");
    return;
  }

  if (doStart && kiraGatewayReady()) {
    kiraGatewayStartListening("auto");
  }

  if (!kiraGatewayReady()) {
    if (doFinish) clearQueue();
    return;
  }

  // Keep each main-loop pass bounded: at most one 60 ms network packet.
  // During normal streaming, wait until all three 20 ms frames are present.
  // Only the final packet may be zero-padded when speech has ended.
  const UBaseType_t waiting = uxQueueMessagesWaiting(queueHandle);
  const bool packetReady = waiting >= 3 || (doFinish && waiting > 0);

  PcmFrame frames[3];
  size_t got = 0;

  if (packetReady) {
    while (got < 3 && xQueueReceive(queueHandle, &frames[got], 0) == pdTRUE) {
      got++;
    }
  }

  if (got > 0) {
    int16_t pcm60[NETWORK_FRAME_SAMPLES];
    memset(pcm60, 0, sizeof(pcm60));

    for (size_t i = 0; i < got; ++i) {
      memcpy(
        pcm60 + i * PCM_FRAME_SAMPLES,
        frames[i].pcm,
        sizeof(frames[i].pcm)
      );
    }

    uint8_t packet[512];
    const size_t encoded = kiraAudioCodecEncodeOpus(
      pcm60,
      NETWORK_FRAME_SAMPLES,
      packet,
      sizeof(packet)
    );

    if (encoded > 0 && kiraGatewaySendAudio(packet, encoded)) {
      sentPackets++;
      sentBytes += (uint32_t)encoded;
    } else if (encoded == 0) {
      droppedFrames += (uint32_t)got;
    }
  }

  if (doFinish) {
    // If frames remain, keep finish pending until the queue is drained.
    if (uxQueueMessagesWaiting(queueHandle) > 0) {
      portENTER_CRITICAL(&stateMux);
      finishRequested = true;
      portEXIT_CRITICAL(&stateMux);
    } else {
      kiraGatewayStopListening();
      clearQueue();
    }
  }
#endif
}

bool kiraAudioStreamActive() {
  bool v;
  portENTER_CRITICAL(&stateMux);
  v = active;
  portEXIT_CRITICAL(&stateMux);
  return v;
}

uint32_t kiraAudioStreamQueuedFrames() {
  return queueHandle ? (uint32_t)uxQueueMessagesWaiting(queueHandle) : 0;
}

uint32_t kiraAudioStreamDroppedFrames() { return droppedFrames; }
uint32_t kiraAudioStreamSentPackets() { return sentPackets; }
uint32_t kiraAudioStreamSentBytes() { return sentBytes; }
