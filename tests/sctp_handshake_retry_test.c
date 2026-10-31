#include "sctp_handshake_retry.h"
#include <assert.h>

int main(void) {
  SctpHandshakeRetry retry = {0};
  const uint8_t init[] = {1, 2, 3, 4};
  assert(!sctp_handshake_due(&retry, 9999, 0));
  sctp_handshake_save(&retry, init, sizeof(init), UINT32_MAX - 500);
  assert(!sctp_handshake_due(&retry, 498, 0));
  assert(sctp_handshake_due(&retry, 499, 0)); // clock wrap
  assert(!sctp_handshake_due(&retry, 499, 1));
  assert(memcmp(retry.bytes, init, sizeof(init)) == 0);
  retry.retries = 2;
  retry.sent_ms = 100;
  assert(!sctp_handshake_due(&retry, 2099, 0));
  assert(sctp_handshake_due(&retry, 2100, 0));
  retry.retries = 5;
  assert(!sctp_handshake_due(&retry, 99999, 0));
  const uint8_t cookie[] = {10, 9, 8};
  sctp_handshake_save(&retry, cookie, sizeof(cookie), 3000);
  assert(retry.retries == 0 && retry.size == sizeof(cookie));
  assert(memcmp(retry.bytes, cookie, sizeof(cookie)) == 0);
  sctp_handshake_save(&retry, cookie, sizeof(retry.bytes) + 1, 0);
  assert(!sctp_handshake_due(&retry, 99999, 0));
  return 0;
}
