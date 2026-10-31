#include "sctp_reliable_queue.h"
#include <assert.h>

int main(void) {
  static SctpReliableQueue q;
  uint8_t packet[4] = {1, 2, 3, 4};
  assert(sctp_reliable_push(&q, packet, sizeof(packet), 0xfffffffeu, 100) == 0);
  packet[0] = 9;
  assert(q.records[q.head].bytes[0] == 1);
  assert(sctp_reliable_push(&q, packet, sizeof(packet), 0xffffffffu, 110) == 0);
  assert(sctp_reliable_push(&q, packet, sizeof(packet), 0, 120) == 0);
  assert(sctp_reliable_ack(&q, 0xfffffffdu) == 0);
  assert(sctp_reliable_ack(&q, 0xffffffffu) == 2);
  assert(q.count == 1 && sctp_reliable_oldest(&q)->tsn == 0);
  assert(!sctp_reliable_retry_due(sctp_reliable_oldest(&q), 319, 200));
  assert(sctp_reliable_retry_due(sctp_reliable_oldest(&q), 320, 200));
  assert(sctp_reliable_ack(&q, 0) == 1);
  for (unsigned i = 0; i < SCTP_RELIABLE_CAPACITY; ++i)
    assert(sctp_reliable_push(&q, packet, sizeof(packet), i + 1, i + 1000) == 0);
  assert(sctp_reliable_push(&q, packet, sizeof(packet), 65, 1100) < 0);
  assert(q.count == SCTP_RELIABLE_CAPACITY);
  assert(sctp_reliable_ack(&q, 32) == 32);
  for (unsigned i = 0; i < 32; ++i)
    assert(sctp_reliable_push(&q, packet, sizeof(packet), i + 65, i + 1200) == 0);
  assert(sctp_reliable_ack(&q, 96) == SCTP_RELIABLE_CAPACITY);
  assert(q.count == 0);
  assert(sctp_reliable_push(&q, packet, SCTP_MTU + 1, 97, 1300) < 0);
  return 0;
}
