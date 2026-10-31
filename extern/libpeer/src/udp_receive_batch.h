#ifndef UDP_RECEIVE_BATCH_H_
#define UDP_RECEIVE_BATCH_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Read-ahead only: the backend must return immediately with packets that are
// already available. Never wait for a full batch and never reorder datagrams.
#define UDP_RECEIVE_BATCH_CAPACITY 8u
#define UDP_RECEIVE_BATCH_BYTES 1300u
#define UDP_RECEIVE_BATCH_ADDRESS_BYTES 128u
#define UDP_RECEIVE_BATCH_UNSUPPORTED (-2)

typedef struct UdpReceiveRecord {
  uint8_t data[UDP_RECEIVE_BATCH_BYTES];
  uint8_t address[UDP_RECEIVE_BATCH_ADDRESS_BYTES];
  unsigned size, address_size;
  int truncated;
} UdpReceiveRecord;

typedef struct UdpReceiveBatch {
  UdpReceiveRecord records[UDP_RECEIVE_BATCH_CAPACITY];
  unsigned next, count;
  uint64_t calls, datagrams, cache_hits, rejected;
  int unsupported;
} UdpReceiveBatch;

typedef int (*UdpReceiveMany)(void*, UdpReceiveRecord*, unsigned);

static inline int udp_receive_batch_read(UdpReceiveBatch* batch,
    uint8_t* data, unsigned capacity, void* address, unsigned* address_size,
    UdpReceiveMany receive_many, void* context) {
  if (batch->unsupported) return UDP_RECEIVE_BATCH_UNSUPPORTED;
  const int cached = batch->next < batch->count;
  if (!cached) {
    ++batch->calls;
    const int count = receive_many(context, batch->records, UDP_RECEIVE_BATCH_CAPACITY);
    if (count == UDP_RECEIVE_BATCH_UNSUPPORTED) batch->unsupported = 1;
    if (count <= 0) return count;
    if ((unsigned)count > UDP_RECEIVE_BATCH_CAPACITY) return -1;
    batch->next = 0; batch->count = (unsigned)count;
    batch->datagrams += (unsigned)count;
  }
  while (batch->next < batch->count) {
    const UdpReceiveRecord* record = &batch->records[batch->next++];
    if (record->size == 0 || record->truncated || record->size > capacity ||
        record->size > UDP_RECEIVE_BATCH_BYTES ||
        record->address_size > UDP_RECEIVE_BATCH_ADDRESS_BYTES ||
        (address && address_size && record->address_size > *address_size)) {
      ++batch->rejected;
      continue; // Never pass a truncated encrypted datagram to DTLS/SRTP.
    }
    memcpy(data, record->data, record->size);
    if (address && address_size) {
      memcpy(address, record->address, record->address_size);
      *address_size = record->address_size;
    }
    if (cached) ++batch->cache_hits;
    return (int)record->size;
  }
  return 0;
}
#endif
