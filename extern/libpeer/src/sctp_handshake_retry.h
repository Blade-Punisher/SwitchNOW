#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Retain the exact wire packet: retries must not change tags, TSNs or cookies.
typedef struct {
  uint8_t bytes[1500];
  size_t size;
  uint32_t sent_ms;
  unsigned retries;
} SctpHandshakeRetry;

static inline void sctp_handshake_save(SctpHandshakeRetry* retry,
    const void* bytes, size_t size, uint32_t now) {
  if (size > sizeof(retry->bytes)) { retry->size = 0; return; }
  memcpy(retry->bytes, bytes, size);
  retry->size = size;
  retry->sent_ms = now;
  retry->retries = 0;
}

static inline int sctp_handshake_due(const SctpHandshakeRetry* retry,
    uint32_t now, int connected) {
  if (connected || !retry->size || retry->retries >= 5) return 0;
  const uint32_t delay = retry->retries < 2 ? 1000u : 2000u;
  return (uint32_t)(now - retry->sent_ms) >= delay;
}
