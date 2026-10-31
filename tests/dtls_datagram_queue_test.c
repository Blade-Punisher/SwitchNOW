#include "dtls_datagram_queue.h"
#include <assert.h>
int main(void) {
    static DtlsDatagramQueue q;
    uint8_t bytes[2] = {1,2};
    assert(dtls_queue_push(&q, bytes, sizeof(bytes), 50) == 2);
    bytes[0] = 99;
    assert(q.records[q.head].data[0] == 1);
    // Backpressure does not consume the head; repeated attempts see the
    // exact same encrypted record until the socket accepts it.
    assert(q.count == 1 && q.records[q.head].queued_ms == 50);
    for (unsigned i=1;i<DTLS_TX_CAPACITY;++i) {
        bytes[0] = (uint8_t)i;
        assert(dtls_queue_push(&q,bytes,2,i+50) == 2);
    }
    assert(dtls_queue_push(&q,bytes,2,100) < 0);
    for (unsigned i=0;i<DTLS_TX_CAPACITY;++i) {
        assert(q.records[q.head].queued_ms == i+50);
        dtls_queue_pop(&q);
    }
    assert(q.count == 0);
    assert(dtls_queue_push(&q,bytes,DTLS_TX_MAX_BYTES+1,200) < 0);
    assert(dtls_queue_push(&q,bytes,2,200) == 2);
    return 0;
}
