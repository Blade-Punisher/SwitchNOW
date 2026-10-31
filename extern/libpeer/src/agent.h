#ifndef AGENT_H_
#define AGENT_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base64.h"
#include "ice.h"
#include "socket.h"
#include "stun.h"
#include "utils.h"

#ifndef AGENT_MAX_DESCRIPTION
#define AGENT_MAX_DESCRIPTION 40960
#endif

#ifndef AGENT_MAX_CANDIDATES
#define AGENT_MAX_CANDIDATES 10
#endif

#ifndef AGENT_MAX_CANDIDATE_PAIRS
#define AGENT_MAX_CANDIDATE_PAIRS 100
#endif

#ifndef AGENT_RTT_SAMPLE_WINDOW
#define AGENT_RTT_SAMPLE_WINDOW 128
#endif

typedef enum AgentState {

  AGENT_STATE_GATHERING_ENDED = 0,
  AGENT_STATE_GATHERING_STARTED,
  AGENT_STATE_GATHERING_COMPLETED,

} AgentState;

typedef enum AgentMode {

  AGENT_MODE_CONTROLLED = 0,
  AGENT_MODE_CONTROLLING

} AgentMode;

typedef struct Agent Agent;

struct Agent {
  char remote_ufrag[ICE_UFRAG_LENGTH + 1];
  char remote_upwd[ICE_UPWD_LENGTH + 1];

  char local_ufrag[ICE_UFRAG_LENGTH + 1];
  char local_upwd[ICE_UPWD_LENGTH + 1];

  IceCandidate local_candidates[AGENT_MAX_CANDIDATES];
  IceCandidate remote_candidates[AGENT_MAX_CANDIDATES];

  int local_candidates_count;
  int remote_candidates_count;

  UdpSocket udp_sockets[2];

  Address host_addr;
  int b_host_addr;
  uint64_t binding_request_time;
  uint32_t last_binding_request_sent_ms;
  uint32_t smoothed_binding_rtt_ms;
  uint32_t rtt_probe_transaction_id[3];
  Address rtt_probe_remote_addr;
  uint32_t rtt_probe_started_ms;
  uint32_t rtt_probe_last_started_ms;
  uint32_t rtt_probe_last_sample_ms;
  uint32_t rtt_probe_sequence;
  uint32_t rtt_current_ms;
  uint32_t recv_select_ms;
  uint32_t recv_read_ms;
  uint32_t recv_stun_ms;
  uint32_t rtt_min_ms;
  uint32_t rtt_median_ms;
  uint32_t rtt_p95_ms;
  uint32_t rtt_jitter_q4;
  uint32_t rtt_samples[AGENT_RTT_SAMPLE_WINDOW];
  uint16_t rtt_sample_count;
  uint16_t rtt_sample_cursor;
  uint32_t rtt_probes_sent;
  uint32_t rtt_probes_matched;
  uint32_t rtt_probes_lost;
  uint32_t rtt_probes_unmatched;
  int rtt_probe_in_flight;
  int rtt_monitoring_started;
  AgentState state;

  AgentMode mode;

  IceCandidatePair candidate_pairs[AGENT_MAX_CANDIDATE_PAIRS];
  IceCandidatePair* selected_pair;
  IceCandidatePair* nominated_pair;

  int candidate_pairs_num;
  int use_candidate;
  uint32_t transaction_id[3];
};

typedef struct AgentCandidatePairStats {
  int total;
  int frozen;
  int inprogress;
  int succeeded;
  int failed;
} AgentCandidatePairStats;

typedef struct AgentRttStats {
  int current_ms;
  int smoothed_ms;
  int min_ms;
  int median_ms;
  int p95_ms;
  int jitter_ms;
  uint32_t sample_age_ms;
  uint32_t samples;
  uint32_t probes_sent;
  uint32_t probes_matched;
  uint32_t probes_lost;
  uint32_t probes_unmatched;
  int probe_in_flight;
} AgentRttStats;

void agent_gather_candidate(Agent* agent, const char* urls, const char* username, const char* credential);

void agent_create_ice_credential(Agent* agent);

void agent_get_local_description(Agent* agent, char* description, int length);

int agent_send(Agent* agent, const uint8_t* buf, int len);

int agent_recv(Agent* agent, uint8_t* buf, int len);

int agent_recv_timeout(Agent* agent, uint8_t* buf, int len, int timeout_ms);

void agent_set_remote_description(Agent* agent, char* description);

int agent_select_candidate_pair(Agent* agent);

int agent_fail_nominated_remote(Agent* agent);

int agent_connectivity_check(Agent* agent);

void agent_clear_candidates(Agent* agent);

int agent_create(Agent* agent);

void agent_destroy(Agent* agent);

void agent_update_candidate_pairs(Agent* agent);

void agent_sort_candidate_pairs(Agent* agent);

void agent_get_candidate_pair_stats(Agent* agent, AgentCandidatePairStats* stats);

int agent_get_rtt_ms(Agent* agent);

void agent_get_rtt_stats(Agent* agent, AgentRttStats* stats);

void agent_rtt_probe_tick(Agent* agent);

#endif  // AGENT_H_
