#include "rtp.h"
#include "rtp_recovery.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t random_state=0x52a19;
static uint32_t next_random(void) {
  random_state=random_state*1664525u+1013904223u; return random_state;
}
uint32_t ports_get_epoch_time(void) { return 123456; }
// Frozen R51 algorithm, intentionally retained as the differential oracle.
static int reference_nack(const RtpDecoder* d,uint16_t* id,uint16_t* mask) {
  const uint16_t evidence=rtp_recovery_nack_evidence_packets(d->access_units_completed);
  if(!d->reorder_has_expected_sequence || d->reorder_buffered_packets<evidence) return 0;
  const uint16_t expected=d->reorder_expected_sequence;
  const unsigned slot=expected%RTP_REORDER_WINDOW;
  if(d->reorder_used[slot] && d->reorder_sequences[slot]==expected) return 0;
  uint16_t furthest=0;
  for(unsigned s=0;s<RTP_REORDER_WINDOW;++s) {
    if(!d->reorder_used[s]) continue;
    const int16_t distance=(int16_t)(d->reorder_sequences[s]-expected);
    if(distance>0 && (uint16_t)distance>furthest) furthest=(uint16_t)distance;
  }
  if(furthest<evidence) return 0;
  *id=expected; *mask=0;
  const uint16_t limit=furthest<17?furthest:17;
  for(uint16_t offset=1;offset<limit;++offset) {
    const uint16_t sequence=(uint16_t)(expected+offset);
    const unsigned s=sequence%RTP_REORDER_WINDOW;
    if(!(d->reorder_used[s] && d->reorder_sequences[s]==sequence))
      *mask|=(uint16_t)(1u<<(offset-1));
  }
  return 1;
}
int main(void) {
  for(unsigned test=0;test<100000;++test) {
    RtpDecoder decoder; memset(&decoder,0,sizeof(decoder));
    decoder.reorder_has_expected_sequence=1;
    decoder.access_units_completed=next_random()%400;
    decoder.reorder_expected_sequence=(uint16_t)next_random();
    for(unsigned offset=1;offset<RTP_REORDER_WINDOW;++offset) {
      if(next_random()%3==0) continue;
      const uint16_t sequence=(uint16_t)(decoder.reorder_expected_sequence+offset);
      const unsigned slot=sequence%RTP_REORDER_WINDOW;
      decoder.reorder_used[slot]=1; decoder.reorder_sequences[slot]=sequence;
      ++decoder.reorder_buffered_packets;
    }
    uint16_t old_id=0,old_mask=0,id=0,mask=0;
    assert(reference_nack(&decoder,&old_id,&old_mask)==rtp_decoder_get_nack(&decoder,&id,&mask));
    assert(old_id==id && old_mask==mask);
  }
  puts("RTP NACK differential tests passed: 100000 reorder layouts, including sequence wrap.");
}
