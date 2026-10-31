#ifndef SCTP_RELIABLE_QUEUE_H
#define SCTP_RELIABLE_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "config.h"

// The internal SCTP backend needs a bounded copy of sent DATA chunks until
// the peer's cumulative SACK advances. U-flagged gamepad packets still consume
// TSNs and must be retransmitted to keep the association's receive window open.
#define SCTP_RELIABLE_CAPACITY 64
typedef struct {
  uint8_t bytes[SCTP_MTU];
  uint32_t tsn;
  uint32_t last_attempt_ms;
  uint16_t length;
  uint8_t retries;
} SctpReliableRecord;

typedef struct {
  SctpReliableRecord records[SCTP_RELIABLE_CAPACITY];
  unsigned head;
  unsigned count;
} SctpReliableQueue;

static inline SctpReliableRecord* sctp_reliable_oldest(SctpReliableQueue* q) {
  return q->count ? &q->records[q->head] : NULL;
}

static inline int sctp_reliable_push(SctpReliableQueue* q, const uint8_t* data,
                                      size_t length, uint32_t tsn, uint32_t now_ms) {
  if (!data || length == 0 || length > SCTP_MTU || q->count >= SCTP_RELIABLE_CAPACITY)
    return -1;
  SctpReliableRecord* record = &q->records[(q->head + q->count) % SCTP_RELIABLE_CAPACITY];
  memcpy(record->bytes, data, length);
  record->length = (uint16_t)length;
  record->tsn = tsn;
  record->last_attempt_ms = now_ms;
  record->retries = 0;
  ++q->count;
  return 0;
}

static inline unsigned sctp_reliable_ack(SctpReliableQueue* q, uint32_t cumulative_tsn) {
  unsigned removed = 0;
  while (q->count && (int32_t)(cumulative_tsn - q->records[q->head].tsn) >= 0) {
    q->head = (q->head + 1) % SCTP_RELIABLE_CAPACITY;
    --q->count;
    ++removed;
  }
  return removed;
}

static inline int sctp_reliable_retry_due(const SctpReliableRecord* record,
                                           uint32_t now_ms, uint32_t rto_ms) {
  return record && (uint32_t)(now_ms - record->last_attempt_ms) >= rto_ms;
}

#endif
