#include "kira_audio_codec.h"

#include "kira_next_config.h"

#if KIRA_OPUS_ENABLED
  #if __has_include(<opus/opus.h>)
    #include <opus/opus.h>
    #define KIRA_LIBOPUS_AVAILABLE 1
  #elif __has_include(<opus.h>)
    #include <opus.h>
    #define KIRA_LIBOPUS_AVAILABLE 1
  #else
    #define KIRA_LIBOPUS_AVAILABLE 0
  #endif
#else
  #define KIRA_LIBOPUS_AVAILABLE 0
#endif

namespace {

bool ready = false;

#if KIRA_LIBOPUS_AVAILABLE
OpusEncoder* encoder = nullptr;
OpusDecoder* decoder = nullptr;
#endif

} // namespace

bool kiraAudioCodecBegin() {
  if (ready) return true;

  Serial.println("[AUDIO CODEC] PCM16 READY");

#if KIRA_OPUS_ENABLED
  #if KIRA_LIBOPUS_AVAILABLE
    int encErr = OPUS_OK;
    int decErr = OPUS_OK;

    encoder = opus_encoder_create(
      16000,
      1,
      OPUS_APPLICATION_VOIP,
      &encErr
    );

    decoder = opus_decoder_create(
      16000,
      1,
      &decErr
    );

    if (!encoder || !decoder || encErr != OPUS_OK || decErr != OPUS_OK) {
      if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
      }
      if (decoder) {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
      }

      Serial.println("[AUDIO CODEC] OPUS INIT FAILED; PCM fallback retained");
      ready = true;
      return true;
    }

    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(KIRA_OPUS_BITRATE));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(KIRA_OPUS_COMPLEXITY));
    opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    Serial.print("[AUDIO CODEC] OPUS READY | 16k mono | bitrate=");
    Serial.println(KIRA_OPUS_BITRATE);
  #else
    Serial.println("[AUDIO CODEC] OPUS requested but libopus headers are missing");
    Serial.println("[AUDIO CODEC] PCM fallback retained; KIRA remains functional");
  #endif
#else
  Serial.println("[AUDIO CODEC] OPUS wrapper compiled / activation OFF");
#endif

  ready = true;
  return true;
}

bool kiraAudioCodecAvailable(KiraAudioCodec codec) {
  if (!ready) kiraAudioCodecBegin();

  if (codec == KIRA_AUDIO_CODEC_PCM16) return true;

  if (codec == KIRA_AUDIO_CODEC_OPUS) {
#if KIRA_LIBOPUS_AVAILABLE
    return encoder != nullptr && decoder != nullptr;
#else
    return false;
#endif
  }

  return false;
}

const char* kiraAudioCodecName(KiraAudioCodec codec) {
  switch (codec) {
    case KIRA_AUDIO_CODEC_PCM16: return "PCM16";
    case KIRA_AUDIO_CODEC_OPUS: return "OPUS";
    default: return "UNKNOWN";
  }
}

KiraAudioCodec kiraAudioCodecNetworkPreference() {
  return kiraAudioCodecAvailable(KIRA_AUDIO_CODEC_OPUS)
    ? KIRA_AUDIO_CODEC_OPUS
    : KIRA_AUDIO_CODEC_PCM16;
}

size_t kiraAudioCodecEncodeOpus(
  const int16_t* pcm,
  size_t samples,
  uint8_t* out,
  size_t outCapacity
) {
#if KIRA_LIBOPUS_AVAILABLE
  if (!pcm || !out || samples == 0 || outCapacity == 0 || !encoder) return 0;
  if (samples > 2880 || outCapacity > 1275) return 0;

  const int n = opus_encode(
    encoder,
    (const opus_int16*)pcm,
    (int)samples,
    out,
    (opus_int32)outCapacity
  );

  return n > 0 ? (size_t)n : 0;
#else
  (void)pcm;
  (void)samples;
  (void)out;
  (void)outCapacity;
  return 0;
#endif
}

size_t kiraAudioCodecDecodeOpus(
  const uint8_t* packet,
  size_t packetBytes,
  int16_t* pcmOut,
  size_t pcmCapacitySamples
) {
#if KIRA_LIBOPUS_AVAILABLE
  if (!packet || packetBytes == 0 || !pcmOut || pcmCapacitySamples == 0 || !decoder) return 0;

  const int n = opus_decode(
    decoder,
    packet,
    (opus_int32)packetBytes,
    (opus_int16*)pcmOut,
    (int)pcmCapacitySamples,
    0
  );

  return n > 0 ? (size_t)n : 0;
#else
  (void)packet;
  (void)packetBytes;
  (void)pcmOut;
  (void)pcmCapacitySamples;
  return 0;
#endif
}

bool kiraAudioCodecOpusBackendPresent() {
#if KIRA_LIBOPUS_AVAILABLE
  return true;
#else
  return false;
#endif
}
