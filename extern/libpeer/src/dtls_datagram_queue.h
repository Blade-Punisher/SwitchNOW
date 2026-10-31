#ifndef DTLS_DATAGRAM_QUEUE_H
#define DTLS_DATAGRAM_QUEUE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Copy encrypted records, not application messages: MbedTLS and SCTP may
// safely reuse their buffers once the callback accepts the record.
#define DTLS_TX_CAPACITY 32
#define DTLS_TX_MAX_BYTES 4096
typedef struct {
  uint8_t data[DTLS_TX_MAX_BYTES];
  size_t size;
  uint32_t queued_ms;
} DtlsQueuedDatagram;
typedef struct {
  DtlsQueuedDatagram records[DTLS_TX_CAPACITY];
  unsigned head, count;
} DtlsDatagramQueue;
static inline int dtls_queue_push(DtlsDatagramQueue* q, const uint8_t* data,
                                  size_t size, uint32_t now_ms) {
  if (!data || size == 0 || size > DTLS_TX_MAX_BYTES || q->count == DTLS_TX_CAPACITY) return -1;
  DtlsQueuedDatagram* entry = &q->records[(q->head + q->count) % DTLS_TX_CAPACITY];
  memcpy(entry->data, data, size);
  entry->size = size;
  entry->queued_ms = now_ms;
  ++q->count;
  return (int)size;
}
static inline void dtls_queue_pop(DtlsDatagramQueue* q) {
  if (q->count) { q->head = (q->head + 1) % DTLS_TX_CAPACITY; --q->count; }
}
#endif
