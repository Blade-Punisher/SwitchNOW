#include "rtp.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t now_ms;
static int completed_frames;

uint32_t ports_get_epoch_time(void) {
  return now_ms;
}

static void on_video_packet(const PeerVideoPacket* packet, void* user_data) {
  (void)user_data;
  assert(packet != NULL);
  assert(packet->size > 4);
  completed_frames++;
}

static size_t make_packet(uint8_t* packet, uint16_t sequence, uint32_t timestamp) {
  memset(packet, 0, 14);
  packet[0] = 0x80;
  packet[1] = 0x80 | 96;
  packet[2] = (uint8_t)(sequence >> 8);
  packet[3] = (uint8_t)sequence;
  packet[4] = (uint8_t)(timestamp >> 24);
  packet[5] = (uint8_t)(timestamp >> 16);
  packet[6] = (uint8_t)(timestamp >> 8);
  packet[7] = (uint8_t)timestamp;
  packet[11] = 1;
  packet[12] = 0x01;
  packet[13] = 0x80;
  return 14;
}

static void decode(RtpDecoder* decoder, uint16_t sequence, uint32_t timestamp) {
  uint8_t packet[14];
  assert(rtp_decoder_decode(decoder, packet,
                            make_packet(packet, sequence, timestamp)) >= 0);
}

int main(void) {
  RtpDecoder decoder;
  rtp_decoder_init(&decoder, CODEC_H264, NULL, NULL);
  rtp_decoder_set_video_callback(&decoder, on_video_packet);

  now_ms = 100;
  decode(&decoder, 100, 90000);
  decode(&decoder, 102, 93000);
  assert(completed_frames == 1);
  assert(decoder.reorder_buffered_packets == 1);

  now_ms += decoder.reorder_gap_hold_ms - 1;
  decode(&decoder, 103, 94500);
  assert(completed_frames == 1);
  assert(decoder.reorder_buffered_packets == 2);

  now_ms += 1;
  rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips == 1);
  assert(decoder.access_units_dropped == 1);
  // 90000 was already completed. Missing sequence101 damages the next AU,
  // 93000, not the frame already delivered to the decoder.
  assert(decoder.last_dropped_timestamp == 93000);
  assert(decoder.reorder_buffered_packets == 0);
  assert(completed_frames == 2);
  rtp_decoder_cleanup(&decoder);

  completed_frames = 0;
  now_ms = 1000;
  rtp_decoder_init(&decoder, CODEC_H264, NULL, NULL);
  rtp_decoder_set_video_callback(&decoder, on_video_packet);
  decode(&decoder, 200, 90000);
  decode(&decoder, 202, 93000);
  decode(&decoder, 201, 91500);
  assert(completed_frames == 3);
  assert(decoder.reordered_packets == 1);
  assert(decoder.forced_sequence_skips == 0);
  assert(decoder.access_units_dropped == 0);
  assert(decoder.reorder_buffered_packets == 0);
  rtp_decoder_cleanup(&decoder);

  completed_frames = 0;
  now_ms = 2000;
  rtp_decoder_init(&decoder, CODEC_H264, NULL, NULL);
  rtp_decoder_set_video_callback(&decoder, on_video_packet);
  decode(&decoder, 65535, 90000);
  decode(&decoder, 1, 93000);
  decode(&decoder, 0, 91500);
  decode(&decoder, 0, 91500);
  assert(completed_frames == 3);
  assert(decoder.reordered_packets == 1);
  assert(decoder.late_packets_dropped == 1);
  assert(decoder.forced_sequence_skips == 0);
  rtp_decoder_cleanup(&decoder);
  return 0;
}
