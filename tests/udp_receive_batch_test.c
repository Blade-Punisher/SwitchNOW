#include "udp_receive_batch.h"
#include <assert.h>
#include <stdio.h>

typedef struct Source { unsigned next, remaining, calls; int result; } Source;
static int receive_many(void* context, UdpReceiveRecord* records, unsigned capacity) {
  Source* source = context;
  ++source->calls;
  if (source->result) return source->result;
  const unsigned count = source->remaining < capacity ? source->remaining : capacity;
  for (unsigned i = 0; i < count; ++i) {
    UdpReceiveRecord* r = &records[i];
    r->size = 3; r->address_size = 2; r->truncated = 0;
    r->data[0] = (uint8_t)source->next++; r->data[1] = 0; r->data[2] = 255;
    r->address[0] = r->data[0]; r->address[1] = 42;
  }
  source->remaining -= count;
  return (int)count;
}
static int read_record(UdpReceiveBatch* batch, Source* source, unsigned expected) {
  uint8_t data[8], address[8]; unsigned size = sizeof(address);
  const int received = udp_receive_batch_read(batch, data, sizeof(data), address, &size, receive_many, source);
  if (received > 0) {
    assert(received == 3 && data[0] == (uint8_t)expected && data[1] == 0 && data[2] == 255);
    assert(size == 2 && address[0] == data[0] && address[1] == 42);
  }
  return received;
}
int main(void) {
  UdpReceiveBatch batch = {0}; Source source = {.remaining=800};
  // Same 800 UDP datagrams, exactly 100 backend calls instead of 800. No
  // synthetic delay or timing benchmark is used to claim a hardware speedup.
  for (unsigned i = 0; i < 800; ++i) assert(read_record(&batch, &source, i) == 3);
  assert(source.calls == 100 && batch.calls == 100 && batch.cache_hits == 700);
  assert(batch.datagrams == 800 && read_record(&batch,&source,0) == 0);
  // A partial batch returns immediately; an empty source is polled only once.
  source.remaining = 1;
  assert(read_record(&batch,&source,800) == 3);
  assert(read_record(&batch,&source,0) == 0 && source.calls == 103);
  source.result = -1;
  assert(read_record(&batch,&source,0) == -1);
  source.result = 0; source.remaining = 2;
  assert(read_record(&batch,&source,801) == 3); // error did not poison the queue
  batch.records[batch.next].truncated = 1;
  assert(read_record(&batch,&source,0) == 0 && batch.rejected == 1);
  // Destination bounds and invalid endpoint sizes cannot produce partial data.
  for (unsigned reason=0;reason<5;++reason) {
    batch.next=0; batch.count=1;
    batch.records[0].size=3; batch.records[0].address_size=2; batch.records[0].truncated=0;
    if(reason==0) batch.records[0].size=9;
    if(reason==1) batch.records[0].size=UDP_RECEIVE_BATCH_BYTES+1;
    if(reason==2) batch.records[0].address_size=9;
    if(reason==3) batch.records[0].address_size=UDP_RECEIVE_BATCH_ADDRESS_BYTES+1;
    if(reason==4) batch.records[0].size=0;
    assert(read_record(&batch,&source,0)==0);
  }
  assert(batch.rejected==6);
  source.result=UDP_RECEIVE_BATCH_UNSUPPORTED;
  assert(read_record(&batch,&source,0)==UDP_RECEIVE_BATCH_UNSUPPORTED);
  const unsigned calls=source.calls;
  assert(read_record(&batch,&source,0)==UDP_RECEIVE_BATCH_UNSUPPORTED && source.calls==calls);
  // Corrupt backend count cannot walk past the fixed record array.
  batch=(UdpReceiveBatch){0}; source.result=UDP_RECEIVE_BATCH_CAPACITY+1;
  assert(read_record(&batch,&source,0)==-1 && batch.count==0);
  puts("UDP read-ahead ordering/bounds/fallback tests passed; 800 datagrams / 100 backend calls.");
}
