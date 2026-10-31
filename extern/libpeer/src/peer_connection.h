/**
 * @file peer_connection.h
 * @brief Struct PeerConnection
 */
#ifndef PEER_CONNECTION_H_
#define PEER_CONNECTION_H_

#include <stdint.h>
#include <stdlib.h>

// Retryable SCTP send-window backpressure, distinct from a closed association.
#define SCTP_SEND_WINDOW_FULL (-2)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SdpType {
  SDP_TYPE_OFFER = 0,
  SDP_TYPE_ANSWER,
} SdpType;

typedef enum PeerConnectionState {

  PEER_CONNECTION_CLOSED = 0,
  PEER_CONNECTION_NEW,
  PEER_CONNECTION_CHECKING,
  PEER_CONNECTION_CONNECTED,
  PEER_CONNECTION_COMPLETED,
  PEER_CONNECTION_FAILED,
  PEER_CONNECTION_DISCONNECTED,

} PeerConnectionState;

typedef enum DataChannelType {

  DATA_CHANNEL_NONE = 0,
  DATA_CHANNEL_STRING,
  DATA_CHANNEL_BINARY,

} DataChannelType;

typedef enum DecpChannelType {
  DATA_CHANNEL_RELIABLE = 0x00,
  DATA_CHANNEL_RELIABLE_UNORDERED = 0x80,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT = 0x01,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED = 0x81,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED = 0x02,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED = 0x82,
} DecpChannelType;

typedef enum MediaCodec {

  CODEC_NONE = 0,

  /* Video */
  CODEC_H264,
  CODEC_VP8,    // not implemented yet
  CODEC_MJPEG,  // not implemented yet

  /* Audio */
  CODEC_OPUS,  // not implemented yet
  CODEC_PCMA,
  CODEC_PCMU,

} MediaCodec;

typedef struct IceServer {
  const char* urls;
  const char* username;
  const char* credential;

} IceServer;

typedef struct PeerAudioPacket {
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
  uint32_t ssrc;
  uint16_t sequence;
  uint8_t payload_type;
  uint8_t marker;
} PeerAudioPacket;

typedef struct PeerVideoPacket {
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
  uint32_t ssrc;
} PeerVideoPacket;

typedef struct PeerVideoRtpStats {
  // IPv4 live receive telemetry; these are socket operations, not RTP loss.
  uint64_t receive_batch_calls, receive_batch_datagrams, receive_batch_hits, receive_batch_rejected;
  int receive_batch_active;
  uint32_t packets_received;
  uint32_t sequence_gaps;
  uint32_t reordered_packets;
  uint32_t late_packets_dropped;
  uint32_t forced_sequence_skips;
  uint32_t reorder_buffered_packets;
  uint32_t startup_sequence_rebases;
  uint32_t reorder_gap_hold_ms;
  uint32_t reorder_burst_gap_hold_ms;
  uint32_t reorder_adaptive_holds;
  uint32_t reorder_gap_recoveries;
  uint32_t reorder_gap_expirations;
  uint32_t reorder_burst_fast_forwards;
  uint32_t access_units_completed;
    uint32_t access_units_dropped;
    uint32_t last_dropped_timestamp;
  uint32_t nack_requests;
  uint32_t nack_packets_requested;
  uint32_t nack_retries;
  uint32_t rtx_packets_received;
  uint32_t rtx_packets_recovered;
  uint32_t rtx_packets_invalid;
} PeerVideoRtpStats;

typedef struct PeerConfiguration {
  IceServer ice_servers[5];

  MediaCodec audio_codec;
  MediaCodec video_codec;
  DataChannelType datachannel;

  void (*onaudiotrack)(uint8_t* data, size_t size, void* userdata);
  void (*onaudiopacket)(const PeerAudioPacket* packet, void* userdata);
  void (*onvideotrack)(uint8_t* data, size_t size, void* userdata);
  void (*onvideopacket)(const PeerVideoPacket* packet, void* userdata);
  void (*onrtpsenderreport)(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp, void* userdata);
  void (*on_request_keyframe)(void* userdata);
  void* user_data;

} PeerConfiguration;

typedef struct PeerConnection PeerConnection;

int peer_connection_get_video_rtp_stats(PeerConnection* pc, PeerVideoRtpStats* stats);

const char* peer_connection_state_to_string(PeerConnectionState state);

PeerConnectionState peer_connection_get_state(PeerConnection* pc);

void* peer_connection_get_sctp(PeerConnection* pc);

PeerConnection* peer_connection_create(PeerConfiguration* config);

void peer_connection_destroy(PeerConnection* pc);

void peer_connection_close(PeerConnection* pc);

// Returns 1 when a media/control datagram was consumed, otherwise 0.
int peer_connection_loop(PeerConnection* pc);

// Drains one already queued datagram without waiting for socket readiness.
int peer_connection_loop_nonblocking(PeerConnection* pc);

int peer_connection_create_datachannel(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol);

int peer_connection_create_datachannel_sid(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol, uint16_t sid);

/**
 * @brief send message to data channel
 * @param[in] peer connection
 * @param[in] message buffer
 * @param[in] length of message
 */
int peer_connection_datachannel_send(PeerConnection* pc, char* message, size_t len);

int peer_connection_datachannel_send_sid(PeerConnection* pc, char* message, size_t len, uint16_t sid);

int peer_connection_datachannel_send_binary_sid(PeerConnection* pc, char* message, size_t len, uint16_t sid);

int peer_connection_send_audio(PeerConnection* pc, const uint8_t* packet, size_t bytes);

int peer_connection_send_video(PeerConnection* pc, const uint8_t* packet, size_t bytes);

int peer_connection_request_video_keyframe(PeerConnection* pc);

void peer_connection_set_remote_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

void peer_connection_set_local_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

const char* peer_connection_create_offer(PeerConnection* pc);

const char* peer_connection_create_answer(PeerConnection* pc);

/**
 * @brief register callback function to handle packet loss from RTCP receiver report
 * @param[in] peer connection
 * @param[in] callback function void (*cb)(float fraction_loss, uint32_t total_loss, void *userdata)
 * @param[in] userdata for callback function
 */
void peer_connection_on_receiver_packet_loss(PeerConnection* pc,
                                             void (*on_receiver_packet_loss)(float fraction_loss, uint32_t total_loss, void* userdata));

/**
 * @brief Set the callback function to handle onicecandidate event.
 * @param A PeerConnection.
 * @param A callback function to handle onicecandidate event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_onicecandidate(PeerConnection* pc, void (*onicecandidate)(char* sdp_text, void* userdata));

/**
 * @brief Set the callback function to handle oniceconnectionstatechange event.
 * @param A PeerConnection.
 * @param A callback function to handle oniceconnectionstatechange event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_oniceconnectionstatechange(PeerConnection* pc,
                                                void (*oniceconnectionstatechange)(PeerConnectionState state, void* userdata));

/**
 * @brief register callback function to handle event of datachannel
 * @param[in] peer connection
 * @param[in] callback function when message received
 * @param[in] callback function when connection is opened
 * @param[in] callback function when connection is closed
 */
void peer_connection_ondatachannel(PeerConnection* pc,
                                   void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid),
                                   void (*onopen)(void* userdata),
                                   void (*onclose)(void* userdata));

int peer_connection_lookup_sid(PeerConnection* pc, const char* label, uint16_t* sid);

char* peer_connection_lookup_sid_label(PeerConnection* pc, uint16_t sid);

/**
 * @brief adds a new remote candidate to the peer connection
 * @param[in] peer connection
 * @param[in] ice candidate
 */
int peer_connection_add_ice_candidate(PeerConnection* pc, char* ice_candidate);

int peer_connection_get_ice_candidate_pair_stats(PeerConnection* pc,
                                                 int* total,
                                                 int* frozen,
                                                 int* inprogress,
                                                 int* succeeded,
                                                 int* failed);

typedef struct PeerIceCandidatePairInfo {
  uint64_t priority;
  int state;
  int local_type;
  int remote_type;
  int connectivity_checks;
  int selected;
  int nominated;
  char local_address[64];
  char remote_address[64];
  int local_port;
  int remote_port;
} PeerIceCandidatePairInfo;

typedef enum PeerRttSource {
  PEER_RTT_SOURCE_NONE = 0,
  PEER_RTT_SOURCE_STUN,
  PEER_RTT_SOURCE_SCTP,
} PeerRttSource;

typedef struct PeerNetworkRttStats {
  int current_ms;
  int stun_current_ms;
  int sctp_current_ms;
  int min_ms;
  int median_ms;
  int p95_ms;
  int jitter_ms;
  uint32_t stun_sample_age_ms;
  uint32_t samples;
  uint32_t probes_sent;
  uint32_t probes_matched;
  uint32_t probes_lost;
  uint32_t probes_unmatched;
  int probe_in_flight;
  PeerRttSource source;
} PeerNetworkRttStats;

int peer_connection_get_ice_candidate_pair_count(PeerConnection* pc);

int peer_connection_get_ice_candidate_pair_info(PeerConnection* pc,
                                                int index,
                                                PeerIceCandidatePairInfo* info);

int peer_connection_get_rtt_ms(PeerConnection* pc);

int peer_connection_get_rtt_stats(PeerConnection* pc,
                                  PeerNetworkRttStats* stats);
// In-memory STUN snapshot only; never queries the usrsctp socket.
int peer_connection_get_stun_rtt_stats(PeerConnection* pc,
                                       PeerNetworkRttStats* stats);

void peer_connection_set_diagnostics_enabled(int enabled);
void peer_connection_set_diagnostics_sink(void (*sink)(const char*, int));
void peer_connection_flush_diagnostics(void);

#ifdef __cplusplus
}
#endif

#endif  // PEER_CONNECTION_H_
