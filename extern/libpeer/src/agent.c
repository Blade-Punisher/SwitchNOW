#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "agent.h"
#include "base64.h"
#include "ice.h"
#include "ice_candidate_pair_policy.h"
#include "ports.h"
#include "socket.h"
#include "udp_receive_batch.h"
#include "stun.h"
#include "utils.h"

#define AGENT_POLL_TIMEOUT 1
#define AGENT_CONNCHECK_MAX 1000
#define AGENT_CONNCHECK_SWITCH 300
#define AGENT_CONNCHECK_PERIOD 100
#define AGENT_STUN_RECV_MAXTIMES 1000
#define AGENT_RTT_PROBE_INTERVAL_MS 1000
#define AGENT_RTT_PROBE_TIMEOUT_MS 1000
#define AGENT_RTT_SAMPLE_MAX_MS 5000

void agent_clear_candidates(Agent* agent) {
  agent->local_candidates_count = 0;
  agent->remote_candidates_count = 0;
  agent->candidate_pairs_num = 0;
  agent->selected_pair = NULL;
  agent->nominated_pair = NULL;
  agent->last_binding_request_sent_ms = 0;
  agent->smoothed_binding_rtt_ms = 0;
  memset(agent->rtt_probe_transaction_id, 0,
         sizeof(agent->rtt_probe_transaction_id));
  memset(&agent->rtt_probe_remote_addr, 0,
         sizeof(agent->rtt_probe_remote_addr));
  agent->rtt_probe_started_ms = 0;
  agent->rtt_probe_last_started_ms = 0;
  agent->rtt_probe_last_sample_ms = 0;
  agent->rtt_probe_sequence = 0;
  agent->rtt_current_ms = 0;
  agent->rtt_min_ms = 0;
  agent->rtt_median_ms = 0;
  agent->rtt_p95_ms = 0;
  agent->rtt_jitter_q4 = 0;
  memset(agent->rtt_samples, 0, sizeof(agent->rtt_samples));
  agent->rtt_sample_count = 0;
  agent->rtt_sample_cursor = 0;
  agent->rtt_probes_sent = 0;
  agent->rtt_probes_matched = 0;
  agent->rtt_probes_lost = 0;
  agent->rtt_probes_unmatched = 0;
  agent->rtt_probe_in_flight = 0;
  agent->rtt_monitoring_started = 0;
}

int agent_create(Agent* agent) {
  int ret;
  if ((ret = udp_socket_open(&agent->udp_sockets[0], AF_INET, 0)) < 0) {
    LOGE("Failed to create UDP socket.");
    return ret;
  }
  LOGI("create IPv4 UDP socket: %d", agent->udp_sockets[0].fd);

#if CONFIG_IPV6
  if ((ret = udp_socket_open(&agent->udp_sockets[1], AF_INET6, 0)) < 0) {
    LOGE("Failed to create IPv6 UDP socket.");
    return ret;
  }
  LOGI("create IPv6 UDP socket: %d", agent->udp_sockets[1].fd);
#endif

  agent_clear_candidates(agent);
  memset(agent->remote_ufrag, 0, sizeof(agent->remote_ufrag));
  memset(agent->remote_upwd, 0, sizeof(agent->remote_upwd));
  return 0;
}

void agent_destroy(Agent* agent) {
  if (agent->udp_sockets[0].fd > 0) {
    udp_socket_close(&agent->udp_sockets[0]);
  }

#if CONFIG_IPV6
  if (agent->udp_sockets[1].fd > 0) {
    udp_socket_close(&agent->udp_sockets[1]);
  }
#endif
}

static int agent_socket_recv_timeout(Agent* agent, Address* addr, uint8_t* buf,
                                     int len, int timeout_ms) {
  int ret = -1;
  int i = 0;
  int maxfd = -1;
  fd_set rfds;
  struct timeval tv;
  int addr_type[] = { AF_INET,
#if CONFIG_IPV6
                      AF_INET6,
#endif
  };

  // Switch's select(0) still performs a socket-service IPC round trip and can
  // stall for tens of milliseconds. These UDP sockets are O_NONBLOCK and the
  // receive wrapper uses MSG_DONTWAIT, so the live worker can read directly.
  int cached = 0;
  for (i = 0; i < (int)(sizeof(addr_type) / sizeof(addr_type[0])); ++i) {
    const UdpReceiveBatch* batch = agent->udp_sockets[i].receive_batch;
    if (batch && batch->next < batch->count) cached = 1;
  }
  // A timed compatibility poll must not wait on the kernel when read-ahead
  // already owns the next datagram, even if the OS socket is now empty.
  if (timeout_ms == 0 || cached) {
    const uint32_t read_started = ports_get_epoch_time();
    int error = 0;
    for (i = 0; i < (int)(sizeof(addr_type) / sizeof(addr_type[0])); ++i) {
      if (agent->udp_sockets[i].fd < 0) continue;
      ret = udp_socket_recvfrom(&agent->udp_sockets[i], addr, buf, len);
      if (ret > 0) {
        agent->recv_read_ms = ports_get_epoch_time() - read_started;
        return ret;
      }
      if (ret < 0) error = ret;
    }
    agent->recv_read_ms = ports_get_epoch_time() - read_started;
    return error;
  }

  tv.tv_sec = 0;
  tv.tv_usec = timeout_ms * 1000;
  FD_ZERO(&rfds);

  for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
    if (agent->udp_sockets[i].fd > maxfd) {
      maxfd = agent->udp_sockets[i].fd;
    }
    if (agent->udp_sockets[i].fd >= 0) {
      FD_SET(agent->udp_sockets[i].fd, &rfds);
    }
  }

  const uint32_t select_started = ports_get_epoch_time();
  ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
  agent->recv_select_ms = ports_get_epoch_time() - select_started;
  if (ret < 0) {
    LOGE("select error");
  } else if (ret == 0) {
    // timeout
  } else {
    for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
      if (agent->udp_sockets[i].fd >= 0 && FD_ISSET(agent->udp_sockets[i].fd, &rfds)) {
        memset(buf, 0, len);
        const uint32_t read_started = ports_get_epoch_time();
        ret = udp_socket_recvfrom(&agent->udp_sockets[i], addr, buf, len);
        agent->recv_read_ms = ports_get_epoch_time() - read_started;
        break;
      }
    }
  }

  return ret;
}

static int agent_socket_recv_attempts(Agent* agent, Address* addr, uint8_t* buf, int len, int maxtimes) {
  int ret = -1;
  int i = 0;
  for (i = 0; i < maxtimes; i++) {
    if ((ret = agent_socket_recv_timeout(agent, addr, buf, len,
                                         AGENT_POLL_TIMEOUT)) != 0) {
      break;
    }
  }
  return ret;
}

static int agent_socket_send(Agent* agent, Address* addr, const uint8_t* buf, int len) {
  switch (addr->family) {
    case AF_INET6:
      return udp_socket_sendto(&agent->udp_sockets[1], addr, buf, len);
    case AF_INET:
    default:
      return udp_socket_sendto(&agent->udp_sockets[0], addr, buf, len);
  }
  return -1;
}

static int agent_create_host_addr(Agent* agent) {
  int i, j;
  const char* iface_prefx[] = {CONFIG_IFACE_PREFIX};
  IceCandidate* ice_candidate;
  int addr_type[] = { AF_INET,
#if CONFIG_IPV6
                      AF_INET6,
#endif
  };

  for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
    for (j = 0; j < sizeof(iface_prefx) / sizeof(iface_prefx[0]); j++) {
      ice_candidate = agent->local_candidates + agent->local_candidates_count;
      // only copy port and family to addr of ice candidate
      ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_HOST,
                           &agent->udp_sockets[i].bind_addr);
      // if resolve host addr, add to local candidate
      if (ports_get_host_addr(&ice_candidate->addr, iface_prefx[j])) {
        agent->local_candidates_count++;
      }
    }
  }

  return 0;
}

static int agent_create_stun_addr(Agent* agent, Address* serv_addr) {
  int ret = -1;
  Address bind_addr;
  StunMessage send_msg;
  StunMessage recv_msg;
  memset(&send_msg, 0, sizeof(send_msg));
  memset(&recv_msg, 0, sizeof(recv_msg));

  stun_msg_create(&send_msg, STUN_CLASS_REQUEST | STUN_METHOD_BINDING);

  ret = agent_socket_send(agent, serv_addr, send_msg.buf, send_msg.size);

  if (ret == -1) {
    LOGE("Failed to send STUN Binding Request.");
    return ret;
  }

  ret = agent_socket_recv_attempts(agent, NULL, recv_msg.buf, sizeof(recv_msg.buf), AGENT_STUN_RECV_MAXTIMES);
  if (ret <= 0) {
    LOGD("Failed to receive STUN Binding Response.");
    return ret;
  }

  stun_parse_msg_buf(&recv_msg);
  memcpy(&bind_addr, &recv_msg.mapped_addr, sizeof(Address));
  IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;
  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_SRFLX, &bind_addr);
  return ret;
}

static int agent_create_turn_addr(Agent* agent, Address* serv_addr, const char* username, const char* credential) {
  int ret = -1;
  uint32_t attr = ntohl(0x11000000);
  Address turn_addr;
  StunMessage send_msg;
  StunMessage recv_msg;
  memset(&recv_msg, 0, sizeof(recv_msg));
  memset(&send_msg, 0, sizeof(send_msg));
  stun_msg_create(&send_msg, STUN_METHOD_ALLOCATE);
  stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_REQUESTED_TRANSPORT, sizeof(attr), (char*)&attr);  // UDP
  stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_USERNAME, strlen(username), (char*)username);

  ret = agent_socket_send(agent, serv_addr, send_msg.buf, send_msg.size);
  if (ret == -1) {
    LOGE("Failed to send TURN Binding Request.");
    return -1;
  }

  ret = agent_socket_recv_attempts(agent, NULL, recv_msg.buf, sizeof(recv_msg.buf), AGENT_STUN_RECV_MAXTIMES);
  if (ret <= 0) {
    LOGD("Failed to receive STUN Binding Response.");
    return ret;
  }

  stun_parse_msg_buf(&recv_msg);

  if (recv_msg.stunclass == STUN_CLASS_ERROR && recv_msg.stunmethod == STUN_METHOD_ALLOCATE) {
    memset(&send_msg, 0, sizeof(send_msg));
    stun_msg_create(&send_msg, STUN_METHOD_ALLOCATE);
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_REQUESTED_TRANSPORT, sizeof(attr), (char*)&attr);  // UDP
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_USERNAME, strlen(username), (char*)username);
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_NONCE, strlen(recv_msg.nonce), recv_msg.nonce);
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_REALM, strlen(recv_msg.realm), recv_msg.realm);
    stun_msg_finish(&send_msg, STUN_CREDENTIAL_LONG_TERM, credential, strlen(credential));
  } else {
    LOGE("Invalid TURN Binding Response.");
    return -1;
  }

  ret = agent_socket_send(agent, serv_addr, send_msg.buf, send_msg.size);
  if (ret < 0) {
    LOGE("Failed to send TURN Binding Request.");
    return -1;
  }

  agent_socket_recv_attempts(agent, NULL, recv_msg.buf, sizeof(recv_msg.buf), AGENT_STUN_RECV_MAXTIMES);
  if (ret <= 0) {
    LOGD("Failed to receive TURN Binding Response.");
    return ret;
  }

  stun_parse_msg_buf(&recv_msg);
  memcpy(&turn_addr, &recv_msg.relayed_addr, sizeof(Address));
  IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;
  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_RELAY, &turn_addr);
  return ret;
}

void agent_gather_candidate(Agent* agent, const char* urls, const char* username, const char* credential) {
  char* pos;
  int port;
  char hostname[64];
  char addr_string[ADDRSTRLEN];
  int i;
  int addr_type[1] = {AF_INET};  // ipv6 no need stun
  Address resolved_addr;
  memset(hostname, 0, sizeof(hostname));

  if (urls == NULL) {
    agent_create_host_addr(agent);
    return;
  }

  if ((pos = strstr(urls + 5, ":")) == NULL) {
    LOGE("Invalid URL");
    return;
  }

  port = atoi(pos + 1);
  if (port <= 0) {
    LOGE("Cannot parse port");
    return;
  }

  snprintf(hostname, pos - urls - 5 + 1, "%s", urls + 5);

  for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
    if (ports_resolve_addr(hostname, &resolved_addr) == 0) {
      addr_set_port(&resolved_addr, port);
      addr_to_string(&resolved_addr, addr_string, sizeof(addr_string));
      LOGI("Resolved stun/turn server %s:%d", addr_string, port);

      if (strncmp(urls, "stun:", 5) == 0) {
        LOGD("Create stun addr");
        agent_create_stun_addr(agent, &resolved_addr);
      } else if (strncmp(urls, "turn:", 5) == 0) {
        LOGD("Create turn addr");
        agent_create_turn_addr(agent, &resolved_addr, username, credential);
      }
    }
  }
}

void agent_create_ice_credential(Agent* agent) {
  memset(agent->local_ufrag, 0, sizeof(agent->local_ufrag));
  memset(agent->local_upwd, 0, sizeof(agent->local_upwd));

  utils_random_string(agent->local_ufrag, 4);
  utils_random_string(agent->local_upwd, 24);
}

void agent_get_local_description(Agent* agent, char* description, int length) {
  for (int i = 0; i < agent->local_candidates_count; i++) {
    ice_candidate_to_description(&agent->local_candidates[i], description + strlen(description), length - strlen(description));
  }

  // remove last \n
  description[strlen(description)] = '\0';
  LOGD("local description:\n%s", description);
}

int agent_send(Agent* agent, const uint8_t* buf, int len) {
  return agent_socket_send(agent, &agent->nominated_pair->remote->addr, buf, len);
}

static void agent_create_binding_response(Agent* agent, StunMessage* msg, Address* addr) {
  int size = 0;
  char username[584];
  char mapped_address[32];
  uint8_t mask[16];
  StunHeader* header;
  stun_msg_create(msg, STUN_CLASS_RESPONSE | STUN_METHOD_BINDING);
  header = (StunHeader*)msg->buf;
  memcpy(header->transaction_id, agent->transaction_id, sizeof(header->transaction_id));
  snprintf(username, sizeof(username), "%s:%s", agent->local_ufrag, agent->remote_ufrag);
  *((uint32_t*)mask) = htonl(MAGIC_COOKIE);
  memcpy(mask + 4, agent->transaction_id, sizeof(agent->transaction_id));
  size = stun_set_mapped_address(mapped_address, mask, addr);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS, size, mapped_address);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_USERNAME, strlen(username), username);
  stun_msg_finish(msg, STUN_CREDENTIAL_SHORT_TERM, agent->local_upwd, strlen(agent->local_upwd));
}

static void agent_create_binding_request(Agent* agent, IceCandidatePair* pair,
                                         StunMessage* msg,
                                         int include_use_candidate) {
  uint64_t tie_breaker = 0;  // always be controlled
  // send binding request
  stun_msg_create(msg, STUN_CLASS_REQUEST | STUN_METHOD_BINDING);
  StunHeader* header = (StunHeader*)msg->buf;
  const uint32_t now_ms = ports_get_epoch_time();
  const uint32_t sequence = ++agent->rtt_probe_sequence;
  header->transaction_id[0] = htonl(now_ms);
  header->transaction_id[1] = htonl(sequence);
  header->transaction_id[2] =
      htonl(now_ms ^ sequence ^ UINT32_C(0x9e3779b9));
  char username[584];
  memset(username, 0, sizeof(username));
  snprintf(username, sizeof(username), "%s:%s", agent->remote_ufrag, agent->local_ufrag);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_USERNAME, strlen(username), username);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_PRIORITY, 4, (char*)&pair->priority);
  if (agent->mode == AGENT_MODE_CONTROLLING) {
    if (include_use_candidate) {
      stun_msg_write_attr(msg, STUN_ATTR_TYPE_USE_CANDIDATE, 0, NULL);
    }
    stun_msg_write_attr(msg, STUN_ATTR_TYPE_ICE_CONTROLLING, 8, (char*)&tie_breaker);
  } else {
    stun_msg_write_attr(msg, STUN_ATTR_TYPE_ICE_CONTROLLED, 8, (char*)&tie_breaker);
  }
  stun_msg_finish(msg, STUN_CREDENTIAL_SHORT_TERM, agent->remote_upwd, strlen(agent->remote_upwd));
}

static int agent_send_rtt_probe(Agent* agent, IceCandidatePair* pair,
                                int include_use_candidate) {
  StunMessage msg;
  uint32_t now_ms;
  if (!agent || !pair || !pair->remote) {
    return -1;
  }

  memset(&msg, 0, sizeof(msg));
  agent_create_binding_request(agent, pair, &msg, include_use_candidate);
  now_ms = ports_get_epoch_time();
  if (agent_socket_send(agent, &pair->remote->addr, msg.buf, msg.size) < 0) {
    // Rate-limit retries even when the socket rejects a probe immediately.
    agent->rtt_probe_last_started_ms = now_ms;
    return -1;
  }

  const StunHeader* header = (const StunHeader*)msg.buf;
  memcpy(agent->rtt_probe_transaction_id, header->transaction_id,
         sizeof(agent->rtt_probe_transaction_id));
  memcpy(&agent->rtt_probe_remote_addr, &pair->remote->addr,
         sizeof(agent->rtt_probe_remote_addr));
  agent->rtt_probe_started_ms = now_ms;
  agent->rtt_probe_last_started_ms = now_ms;
  agent->last_binding_request_sent_ms = now_ms;
  agent->rtt_probe_in_flight = 1;
  agent->rtt_probes_sent++;
  return 0;
}

static void agent_refresh_rtt_percentile(Agent* agent) {
  uint32_t sorted[AGENT_RTT_SAMPLE_WINDOW];
  const uint16_t count = agent->rtt_sample_count;
  for (uint16_t i = 0; i < count; i++) {
    sorted[i] = agent->rtt_samples[i];
  }
  for (uint16_t i = 1; i < count; i++) {
    const uint32_t value = sorted[i];
    uint16_t j = i;
    while (j > 0 && sorted[j - 1] > value) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = value;
  }
  if (count > 0) {
    agent->rtt_median_ms = count % 2 == 0
        ? (sorted[count / 2 - 1] + sorted[count / 2] + 1) / 2
        : sorted[count / 2];
    const uint16_t percentile_index =
        (uint16_t)(((uint32_t)count * 95 + 99) / 100 - 1);
    agent->rtt_p95_ms = sorted[percentile_index];
  }
}

static void agent_record_rtt_sample(Agent* agent, uint32_t sample_ms,
                                    uint32_t now_ms) {
  if (!agent || sample_ms == 0 || sample_ms > AGENT_RTT_SAMPLE_MAX_MS) {
    return;
  }

  if (agent->rtt_current_ms != 0) {
    const uint32_t difference =
        sample_ms > agent->rtt_current_ms
            ? sample_ms - agent->rtt_current_ms
            : agent->rtt_current_ms - sample_ms;
    const uint32_t previous_jitter = (agent->rtt_jitter_q4 + 8) >> 4;
    if (difference >= previous_jitter) {
      agent->rtt_jitter_q4 += difference - previous_jitter;
    } else {
      agent->rtt_jitter_q4 -= previous_jitter - difference;
    }
  }

  agent->rtt_current_ms = sample_ms;
  agent->rtt_min_ms =
      agent->rtt_min_ms == 0 || sample_ms < agent->rtt_min_ms
          ? sample_ms
          : agent->rtt_min_ms;
  agent->smoothed_binding_rtt_ms =
      agent->smoothed_binding_rtt_ms == 0
          ? sample_ms
          : (agent->smoothed_binding_rtt_ms * 7 + sample_ms) / 8;
  agent->rtt_probe_last_sample_ms = now_ms;
  agent->rtt_samples[agent->rtt_sample_cursor] = sample_ms;
  agent->rtt_sample_cursor =
      (uint16_t)((agent->rtt_sample_cursor + 1) % AGENT_RTT_SAMPLE_WINDOW);
  if (agent->rtt_sample_count < AGENT_RTT_SAMPLE_WINDOW) {
    agent->rtt_sample_count++;
  }
  // Recalculate only when a new sample arrives, never from the render thread.
  agent_refresh_rtt_percentile(agent);
}

void agent_process_stun_request(Agent* agent, StunMessage* stun_msg, Address* addr) {
  StunMessage msg;
  StunHeader* header;
  switch (stun_msg->stunmethod) {
    case STUN_METHOD_BINDING:
      if (stun_msg_is_valid(stun_msg->buf, stun_msg->size, agent->local_upwd) == 0) {
        header = (StunHeader*)stun_msg->buf;
        memcpy(agent->transaction_id, header->transaction_id, sizeof(header->transaction_id));
        agent_create_binding_response(agent, &msg, addr);
        agent_socket_send(agent, addr, msg.buf, msg.size);
        agent->binding_request_time = ports_get_epoch_time();
      }
      break;
    default:
      break;
  }
}

void agent_process_stun_response(Agent* agent, StunMessage* stun_msg,
                                 Address* addr) {
  switch (stun_msg->stunmethod) {
    case STUN_METHOD_BINDING:
      if (stun_msg_is_valid(stun_msg->buf, stun_msg->size, agent->remote_upwd) == 0) {
        const uint32_t now_ms = ports_get_epoch_time();
        const StunHeader* header = (const StunHeader*)stun_msg->buf;
        const int transaction_matches =
            agent->rtt_probe_in_flight &&
            memcmp(header->transaction_id,
                   agent->rtt_probe_transaction_id,
                   sizeof(agent->rtt_probe_transaction_id)) == 0;
        const int address_matches =
            transaction_matches &&
            addr_equal(addr, &agent->rtt_probe_remote_addr);
        if (!transaction_matches || !address_matches) {
          agent->rtt_probes_unmatched++;
          // ICE connectivity checks may retransmit while an older response is
          // still in flight. Such a response can establish the pair, but must
          // never be used as an RTT sample because its send time is ambiguous.
          if (!agent->selected_pair && agent->nominated_pair &&
              agent->nominated_pair->state ==
                  ICE_CANDIDATE_STATE_INPROGRESS &&
              addr_equal(addr, &agent->nominated_pair->remote->addr)) {
            agent->nominated_pair->state = ICE_CANDIDATE_STATE_SUCCEEDED;
            agent->binding_request_time = now_ms;
          }
          break;
        }

        const uint32_t sample_ms = now_ms - agent->rtt_probe_started_ms;
        agent_record_rtt_sample(agent, sample_ms, now_ms);
        agent->rtt_probes_matched++;
        agent->rtt_probe_in_flight = 0;
        agent->rtt_probe_started_ms = 0;
        agent->last_binding_request_sent_ms = 0;
        if (agent->nominated_pair) {
          agent->nominated_pair->state = ICE_CANDIDATE_STATE_SUCCEEDED;
        }
        agent->binding_request_time = now_ms;
      }
      break;
    default:
      break;
  }
}

int agent_recv_timeout(Agent* agent, uint8_t* buf, int len, int timeout_ms) {
  agent->recv_select_ms = agent->recv_read_ms = agent->recv_stun_ms = 0;
  int ret = -1;
  StunMessage stun_msg;
  Address addr;
  if ((ret = agent_socket_recv_timeout(agent, &addr, buf, len, timeout_ms)) > 0) {
    agent->binding_request_time = ports_get_epoch_time();
  }
  if (ret > 0 && stun_probe(buf, len) == 0) {
    const uint32_t stun_started = ports_get_epoch_time();
    memcpy(stun_msg.buf, buf, ret);
    stun_msg.size = ret;
    stun_parse_msg_buf(&stun_msg);
    switch (stun_msg.stunclass) {
      case STUN_CLASS_REQUEST:
        agent_process_stun_request(agent, &stun_msg, &addr);
        break;
      case STUN_CLASS_RESPONSE:
        agent_process_stun_response(agent, &stun_msg, &addr);
        break;
      case STUN_CLASS_ERROR:
        break;
      default:
        break;
    }
    ret = 0;
    agent->recv_stun_ms = ports_get_epoch_time() - stun_started;
  }
  return ret;
}

int agent_recv(Agent* agent, uint8_t* buf, int len) {
  return agent_recv_timeout(agent, buf, len, AGENT_POLL_TIMEOUT);
}

void agent_set_remote_description(Agent* agent, char* description) {
  /*
  a=ice-ufrag:Iexb
  a=ice-pwd:IexbSoY7JulyMbjKwISsG9
  a=candidate:1 1 UDP 1 36.231.28.50 38143 typ srflx
  */
  int i;

  LOGD("Set remote description:\n%s", description);

  char* line_start = description;
  char* line_end = NULL;

  while ((line_end = strstr(line_start, "\r\n")) != NULL) {
    if (strncmp(line_start, "a=ice-ufrag:", strlen("a=ice-ufrag:")) == 0) {
      strncpy(agent->remote_ufrag, line_start + strlen("a=ice-ufrag:"), line_end - line_start - strlen("a=ice-ufrag:"));

    } else if (strncmp(line_start, "a=ice-pwd:", strlen("a=ice-pwd:")) == 0) {
      strncpy(agent->remote_upwd, line_start + strlen("a=ice-pwd:"), line_end - line_start - strlen("a=ice-pwd:"));

    } else if (strncmp(line_start, "a=candidate:", strlen("a=candidate:")) == 0) {
      if (ice_candidate_from_description(&agent->remote_candidates[agent->remote_candidates_count], line_start, line_end) == 0) {
        for (i = 0; i < agent->remote_candidates_count; i++) {
          if (strcmp(agent->remote_candidates[i].foundation, agent->remote_candidates[agent->remote_candidates_count].foundation) == 0) {
            break;
          }
        }
        if (i == agent->remote_candidates_count) {
          agent->remote_candidates_count++;
        }
      }
    }

    line_start = line_end + 2;
  }

  LOGD("remote ufrag: %s", agent->remote_ufrag);
  LOGD("remote upwd: %s", agent->remote_upwd);
}

void agent_sort_candidate_pairs(Agent* agent) {
  int i;
  if (!agent || agent->nominated_pair || agent->selected_pair)
    return;

  for (i = 1; i < agent->candidate_pairs_num; ++i) {
    IceCandidatePair candidate = agent->candidate_pairs[i];
    int insertion = i;
    while (insertion > 0 &&
           agent->candidate_pairs[insertion - 1].priority < candidate.priority) {
      agent->candidate_pairs[insertion] =
          agent->candidate_pairs[insertion - 1];
      --insertion;
    }
    agent->candidate_pairs[insertion] = candidate;
  }
}

void agent_update_candidate_pairs(Agent* agent) {
  int i, j;
  // Please set gather candidates before set remote description
  for (i = 0; i < agent->local_candidates_count; i++) {
    for (j = 0; j < agent->remote_candidates_count; j++) {
      if (agent->local_candidates[i].addr.family == agent->remote_candidates[j].addr.family) {
        if (agent->candidate_pairs_num >= AGENT_MAX_CANDIDATE_PAIRS) {
          LOGE("candidate pair table is full");
          return;
        }
        agent->candidate_pairs[agent->candidate_pairs_num].local = &agent->local_candidates[i];
        agent->candidate_pairs[agent->candidate_pairs_num].remote = &agent->remote_candidates[j];
        agent->candidate_pairs[agent->candidate_pairs_num].priority =
            ice_candidate_pair_priority(
                agent->local_candidates[i].priority,
                agent->remote_candidates[j].priority,
                agent->mode == AGENT_MODE_CONTROLLING);
        agent->candidate_pairs[agent->candidate_pairs_num].state = ICE_CANDIDATE_STATE_FROZEN;
        agent->candidate_pairs_num++;
      }
    }
  }
  agent_sort_candidate_pairs(agent);
  LOGD("candidate pairs num: %d", agent->candidate_pairs_num);
}

static int agent_has_frozen_candidate_pair(Agent* agent, int start) {
  int i;
  for (i = start; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_FROZEN) {
      return 1;
    }
  }
  return 0;
}

int agent_connectivity_check(Agent* agent) {
  char addr_string[ADDRSTRLEN];
  uint8_t buf[1400];

  if (agent->nominated_pair->state != ICE_CANDIDATE_STATE_INPROGRESS) {
    LOGI("nominated pair is not in progress");
    return -1;
  }

  if (agent->nominated_pair->conncheck % AGENT_CONNCHECK_PERIOD == 0) {
    addr_to_string(&agent->nominated_pair->remote->addr, addr_string, sizeof(addr_string));
    LOGD("send binding request to remote ip: %s, port: %d", addr_string, agent->nominated_pair->remote->addr.port);
    (void)agent_send_rtt_probe(agent, agent->nominated_pair, 1);
  }

  agent_recv(agent, buf, sizeof(buf));

  if (agent->nominated_pair->state == ICE_CANDIDATE_STATE_SUCCEEDED) {
    agent->selected_pair = agent->nominated_pair;
    return 0;
  }

  return -1;
}

int agent_select_candidate_pair(Agent* agent) {
  int i;
  for (i = 0; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_FROZEN) {
      // nominate this pair
      agent->nominated_pair = &agent->candidate_pairs[i];
      agent->candidate_pairs[i].conncheck = 0;
      agent->candidate_pairs[i].state = ICE_CANDIDATE_STATE_INPROGRESS;
      return 0;
    } else if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_INPROGRESS) {
      agent->candidate_pairs[i].conncheck++;
      if (agent->candidate_pairs[i].conncheck >= AGENT_CONNCHECK_SWITCH &&
          agent_has_frozen_candidate_pair(agent, i + 1)) {
        agent->candidate_pairs[i].state = ICE_CANDIDATE_STATE_FAILED;
        continue;
      }
      if (agent->candidate_pairs[i].conncheck < AGENT_CONNCHECK_MAX) {
        return 0;
      }
      agent->candidate_pairs[i].state = ICE_CANDIDATE_STATE_FAILED;
    } else if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_FAILED) {
    } else if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_SUCCEEDED) {
      agent->selected_pair = &agent->candidate_pairs[i];
      return 0;
    }
  }
  // all candidate pairs are failed
  return -1;
}

int agent_fail_nominated_remote(Agent* agent) {
  int failed = 0;
  IceCandidate* remote;
  int i;

  if (!agent || !agent->nominated_pair || !agent->nominated_pair->remote) {
    return 0;
  }

  remote = agent->nominated_pair->remote;
  for (i = 0; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].remote == remote) {
      agent->candidate_pairs[i].state = ICE_CANDIDATE_STATE_FAILED;
      failed++;
    }
  }

  agent->selected_pair = NULL;
  agent->nominated_pair = NULL;
  return failed;
}

void agent_get_candidate_pair_stats(Agent* agent, AgentCandidatePairStats* stats) {
  if (!agent || !stats) {
    return;
  }

  memset(stats, 0, sizeof(*stats));
  stats->total = agent->candidate_pairs_num;
  for (int i = 0; i < agent->candidate_pairs_num; i++) {
    switch (agent->candidate_pairs[i].state) {
      case ICE_CANDIDATE_STATE_FROZEN:
        stats->frozen++;
        break;
      case ICE_CANDIDATE_STATE_INPROGRESS:
        stats->inprogress++;
        break;
      case ICE_CANDIDATE_STATE_SUCCEEDED:
        stats->succeeded++;
        break;
      case ICE_CANDIDATE_STATE_FAILED:
        stats->failed++;
        break;
      case ICE_CANDIDATE_STATE_WAITING:
      default:
        break;
    }
  }
}

int agent_get_rtt_ms(Agent* agent) {
  return agent && agent->rtt_current_ms > 0
      ? (int)agent->rtt_current_ms
      : -1;
}

void agent_get_rtt_stats(Agent* agent, AgentRttStats* stats) {
  if (!stats) {
    return;
  }

  memset(stats, 0, sizeof(*stats));
  stats->current_ms = -1;
  stats->smoothed_ms = -1;
  stats->min_ms = -1;
  stats->median_ms = -1;
  stats->p95_ms = -1;
  stats->jitter_ms = -1;
  if (!agent) {
    return;
  }

  stats->current_ms =
      agent->rtt_current_ms > 0 ? (int)agent->rtt_current_ms : -1;
  stats->smoothed_ms = agent->smoothed_binding_rtt_ms > 0
                           ? (int)agent->smoothed_binding_rtt_ms
                           : -1;
  stats->min_ms = agent->rtt_min_ms > 0 ? (int)agent->rtt_min_ms : -1;
  stats->median_ms = agent->rtt_median_ms > 0 ? (int)agent->rtt_median_ms : -1;
  stats->p95_ms = agent->rtt_p95_ms > 0 ? (int)agent->rtt_p95_ms : -1;
  stats->jitter_ms = agent->rtt_sample_count > 1
                         ? (int)((agent->rtt_jitter_q4 + 8) >> 4)
                         : 0;
  stats->sample_age_ms =
      agent->rtt_probe_last_sample_ms > 0
          ? ports_get_epoch_time() - agent->rtt_probe_last_sample_ms
          : UINT32_MAX;
  stats->samples = agent->rtt_probes_matched;
  stats->probes_sent = agent->rtt_probes_sent;
  stats->probes_matched = agent->rtt_probes_matched;
  stats->probes_lost = agent->rtt_probes_lost;
  stats->probes_unmatched = agent->rtt_probes_unmatched;
  stats->probe_in_flight = agent->rtt_probe_in_flight;
}

void agent_rtt_probe_tick(Agent* agent) {
  if (!agent || !agent->selected_pair || !agent->selected_pair->remote) {
    return;
  }

  const uint32_t now_ms = ports_get_epoch_time();
  if (!agent->rtt_monitoring_started) {
    // Initial ICE checks can be retransmitted, so their timing is not suitable
    // for live telemetry. Start a clean sample window on the selected pair.
    agent->rtt_probe_in_flight = 0;
    agent->rtt_probe_started_ms = 0;
    agent->rtt_probe_last_started_ms = 0;
    agent->rtt_probe_last_sample_ms = 0;
    agent->last_binding_request_sent_ms = 0;
    agent->smoothed_binding_rtt_ms = 0;
    agent->rtt_current_ms = 0;
    agent->rtt_min_ms = 0;
    agent->rtt_median_ms = 0;
    agent->rtt_p95_ms = 0;
    agent->rtt_jitter_q4 = 0;
    memset(agent->rtt_samples, 0, sizeof(agent->rtt_samples));
    agent->rtt_sample_count = 0;
    agent->rtt_sample_cursor = 0;
    agent->rtt_probes_sent = 0;
    agent->rtt_probes_matched = 0;
    agent->rtt_probes_lost = 0;
    agent->rtt_probes_unmatched = 0;
    agent->rtt_monitoring_started = 1;
  }

  if (agent->rtt_probe_in_flight &&
      now_ms - agent->rtt_probe_started_ms >= AGENT_RTT_PROBE_TIMEOUT_MS) {
    agent->rtt_probe_in_flight = 0;
    agent->rtt_probe_started_ms = 0;
    agent->last_binding_request_sent_ms = 0;
    agent->rtt_probes_lost++;
  }

  if (!agent->rtt_probe_in_flight &&
      (agent->rtt_probe_last_started_ms == 0 ||
       now_ms - agent->rtt_probe_last_started_ms >=
           AGENT_RTT_PROBE_INTERVAL_MS)) {
    (void)agent_send_rtt_probe(agent, agent->selected_pair, 0);
  }
}
