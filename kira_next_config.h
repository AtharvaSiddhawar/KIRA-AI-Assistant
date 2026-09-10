#pragma once

// ============================================================
// KIRA NEXT - CENTRAL FEATURE / TUNING CONFIG
// GROUP B V3 CAPABILITY CHECK: protocol + Opus + WebSocket libraries verified safely
// ============================================================
//
// IMPORTANT:
// - Existing direct-provider KIRA remains authoritative by default.
// - Group B infrastructure is compiled now, but remote WebSocket/Gateway
//   activation stays OFF until a real endpoint is configured and tested.
// - No Wi-Fi credentials or API keys belong in this file.
// ============================================================

#ifndef KIRA_NEXT_ENABLED
#define KIRA_NEXT_ENABLED 1
#endif

// ------------------------------------------------------------
// GROUP A - FOUNDATION (already active)
// ------------------------------------------------------------
#ifndef KIRA_EVENT_BUS_ENABLED
#define KIRA_EVENT_BUS_ENABLED 1
#endif

#ifndef KIRA_STATE_MACHINE_ENABLED
#define KIRA_STATE_MACHINE_ENABLED 1
#endif

#ifndef KIRA_METRICS_ENABLED
#define KIRA_METRICS_ENABLED 1
#endif

#ifndef KIRA_EVENT_QUEUE_DEPTH
#define KIRA_EVENT_QUEUE_DEPTH 24
#endif

#ifndef KIRA_METRICS_PERIOD_MS
#define KIRA_METRICS_PERIOD_MS 30000UL
#endif

#ifndef KIRA_METRICS_PERIODIC_PRINT
#define KIRA_METRICS_PERIODIC_PRINT 0
#endif

#ifndef KIRA_AUDIO_ENGINE_ENABLED
#define KIRA_AUDIO_ENGINE_ENABLED 1
#endif

#ifndef KIRA_VAD_V2_ENABLED
#define KIRA_VAD_V2_ENABLED 1
#endif

#ifndef KIRA_WAKE_SERVICE_ENABLED
#define KIRA_WAKE_SERVICE_ENABLED 1
#endif

#ifndef KIRA_STREAMING_TTS_ENABLED
#define KIRA_STREAMING_TTS_ENABLED 1
#endif

#ifndef KIRA_ROUTER_V2_ENABLED
#define KIRA_ROUTER_V2_ENABLED 1
#endif

#ifndef KIRA_STATE_BUS_ENABLED
#define KIRA_STATE_BUS_ENABLED 1
#endif

#ifndef KIRA_SESSION_MANAGER_ENABLED
#define KIRA_SESSION_MANAGER_ENABLED 1
#endif

#ifndef KIRA_INTERRUPT_ENABLED
#define KIRA_INTERRUPT_ENABLED 1
#endif

#ifndef KIRA_FULL_DUPLEX_BARGE_IN_ENABLED
#define KIRA_FULL_DUPLEX_BARGE_IN_ENABLED 0
#endif

#ifndef KIRA_TYPED_TOOLS_ENABLED
#define KIRA_TYPED_TOOLS_ENABLED 1
#endif

#ifndef KIRA_RECOVERY_ENABLED
#define KIRA_RECOVERY_ENABLED 1
#endif

#ifndef KIRA_DIAGNOSTICS_ENABLED
#define KIRA_DIAGNOSTICS_ENABLED 1
#endif

// ------------------------------------------------------------
// GROUP B - NEXT 4: PROTOCOL / TRANSPORT CORE
// ------------------------------------------------------------

// The protocol codec/parser is safe to compile and self-test even while
// KIRA is using its existing direct HTTP provider paths.
#ifndef KIRA_PROTOCOL_CORE_ENABLED
#define KIRA_PROTOCOL_CORE_ENABLED 1
#endif

#ifndef KIRA_PROTOCOL_SELFTEST_ENABLED
#define KIRA_PROTOCOL_SELFTEST_ENABLED 1
#endif

// WebSocket code is compiled in this checkpoint only to verify that the
// Arduino WebSocketsClient dependency is present. The gateway remains OFF,
// so no remote connection is attempted and direct-provider KIRA stays active.
#ifndef KIRA_WEBSOCKET_ENABLED
#define KIRA_WEBSOCKET_ENABLED 1
#endif

#ifndef KIRA_GATEWAY_ENABLED
#define KIRA_GATEWAY_ENABLED 0
#endif

// ------------------------------------------------------------
// GROUP B - NEXT 5: OPUS CODEC
// ------------------------------------------------------------

// Compile/probe the real libopus backend. If the library is missing, the
// wrapper reports it and safely retains PCM/direct-provider operation.
#ifndef KIRA_OPUS_ENABLED
#define KIRA_OPUS_ENABLED 1
#endif

#ifndef KIRA_OPUS_BITRATE
#define KIRA_OPUS_BITRATE 24000
#endif

#ifndef KIRA_OPUS_COMPLEXITY
#define KIRA_OPUS_COMPLEXITY 5
#endif

// XiaoZhi-style audio packet target: 60 ms at 16 kHz mono = 960 samples.
#ifndef KIRA_NETWORK_AUDIO_FRAME_MS
#define KIRA_NETWORK_AUDIO_FRAME_MS 60
#endif

// ------------------------------------------------------------
// GROUP B - NEXT 6: NON-BLOCKING AUDIO UPLINK QUEUE
// ------------------------------------------------------------
#ifndef KIRA_AUDIO_UPLINK_ENABLED
#define KIRA_AUDIO_UPLINK_ENABLED 0
#endif

// 20 ms PCM frames. 18 frames ~= 360 ms of bounded buffering.
#ifndef KIRA_AUDIO_UPLINK_QUEUE_DEPTH
#define KIRA_AUDIO_UPLINK_QUEUE_DEPTH 18
#endif

#ifndef KIRA_AUDIO_UPLINK_END_SILENCE_MS
#define KIRA_AUDIO_UPLINK_END_SILENCE_MS 1100UL
#endif

// PCM network transmission is deliberately disabled. The queue waits for
// real Opus because the XiaoZhi WebSocket audio contract uses Opus frames.
#ifndef KIRA_AUDIO_UPLINK_ALLOW_PCM_DEBUG
#define KIRA_AUDIO_UPLINK_ALLOW_PCM_DEBUG 0
#endif

// ------------------------------------------------------------
// STABILITY CHECKPOINT V2
// ------------------------------------------------------------
// Keep live Gateway/audio-uplink activation dormant while dependencies are
// verified. Runtime Wi-Fi recovery must never reboot KIRA or power-cycle the
// Wi-Fi radio.
#ifndef KIRA_WIFI_DNS_PREWARM_ENABLED
#define KIRA_WIFI_DNS_PREWARM_ENABLED 0
#endif

#ifndef KIRA_NETWORK_ACTIVE_HEALTH_PROBE_ENABLED
#define KIRA_NETWORK_ACTIVE_HEALTH_PROBE_ENABLED 0
#endif

#ifndef KIRA_NETWORK_RUNTIME_RADIO_CYCLE_ENABLED
#define KIRA_NETWORK_RUNTIME_RADIO_CYCLE_ENABLED 0
#endif

// ------------------------------------------------------------
// RECOVERY / HEALTH (Group A)
// ------------------------------------------------------------
#ifndef KIRA_RECOVERY_AUTO_REBOOT
#define KIRA_RECOVERY_AUTO_REBOOT 0
#endif

#ifndef KIRA_RECOVERY_ERROR_GRACE_MS
#define KIRA_RECOVERY_ERROR_GRACE_MS 5000UL
#endif

#ifndef KIRA_RECOVERY_LISTENING_MAX_MS
#define KIRA_RECOVERY_LISTENING_MAX_MS 25000UL
#endif

#ifndef KIRA_RECOVERY_ROUTING_MAX_MS
#define KIRA_RECOVERY_ROUTING_MAX_MS 30000UL
#endif

#ifndef KIRA_RECOVERY_STT_MAX_MS
#define KIRA_RECOVERY_STT_MAX_MS 45000UL
#endif

#ifndef KIRA_RECOVERY_AI_MAX_MS
#define KIRA_RECOVERY_AI_MAX_MS 90000UL
#endif

#ifndef KIRA_RECOVERY_ACTION_MAX_MS
#define KIRA_RECOVERY_ACTION_MAX_MS 30000UL
#endif

#ifndef KIRA_RECOVERY_TTS_MAX_MS
#define KIRA_RECOVERY_TTS_MAX_MS 120000UL
#endif

#ifndef KIRA_RECOVERY_RECONNECT_MAX_MS
#define KIRA_RECOVERY_RECONNECT_MAX_MS 120000UL
#endif

#ifndef KIRA_HEALTH_WARN_HEAP_BYTES
#define KIRA_HEALTH_WARN_HEAP_BYTES 32768UL
#endif

#ifndef KIRA_HEALTH_WARN_LARGEST_HEAP_BYTES
#define KIRA_HEALTH_WARN_LARGEST_HEAP_BYTES 12288UL
#endif

#ifndef KIRA_HEALTH_WARN_PSRAM_BYTES
#define KIRA_HEALTH_WARN_PSRAM_BYTES 262144UL
#endif
