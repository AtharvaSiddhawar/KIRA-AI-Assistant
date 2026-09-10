#pragma once

// ============================================================
// KIRA HYBRID TTS V1 CONFIG
// ============================================================
// Provider order:
//   1) Groq Orpheus / Diana (existing primary)
//   2) Edge-TTS / Emma      (optional LAN/online backup)
//   3) PicoTTS              (optional true-offline backup)
//   4) Existing beep fallback
//
// No Wi-Fi passwords or Groq secrets belong in this file.
// ============================================================


// ------------------------------------------------------------
// EDGE-TTS BACKUP
// ------------------------------------------------------------
// Set to 1 only after an OpenAI-compatible Edge-TTS server is
// running on your LAN or another trusted server.
//
// Example:
//   http://192.168.0.50:5050/v1/audio/speech
// ------------------------------------------------------------

#define KIRA_EDGE_TTS_ENABLED 0

#define KIRA_EDGE_TTS_URL \
  "http://192.168.0.50:5050/v1/audio/speech"

#define KIRA_EDGE_TTS_VOICE \
  "en-US-EmmaMultilingualNeural"

// Leave empty when the local server does not require a bearer key.
#define KIRA_EDGE_TTS_API_KEY ""


// ------------------------------------------------------------
// PICOTTS TRUE-OFFLINE BACKUP
// ------------------------------------------------------------
// esp-picotts is an ESP-IDF component, not a normal Arduino
// Library Manager library. Leave this 0 until picotts.h + its
// English language resources have actually been integrated.
// ------------------------------------------------------------

#define KIRA_PICOTTS_ENABLED 0
#define KIRA_PICOTTS_CORE 0
#define KIRA_PICOTTS_PRIORITY 4
