#pragma once

// ============================================================
// KIRA NEXT GROUP 2 — OPTIONAL REMOTE GATEWAY CONFIG
// ============================================================
//
// Intentionally BLANK by default.
// KIRA must continue to work directly with its existing providers.
//
// A future cloud/VPS KIRA Gateway can be configured here.
// Do NOT put Groq/Gemini/API credentials in this file.
// ============================================================

#ifndef KIRA_GATEWAY_HOST
#define KIRA_GATEWAY_HOST ""
#endif

#ifndef KIRA_GATEWAY_PORT
#define KIRA_GATEWAY_PORT 443
#endif

#ifndef KIRA_GATEWAY_PATH
#define KIRA_GATEWAY_PATH "/kira"
#endif

#ifndef KIRA_GATEWAY_USE_TLS
#define KIRA_GATEWAY_USE_TLS 1
#endif
