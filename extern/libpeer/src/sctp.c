#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dtls_srtp.h"
#include "ports.h"
#include "sctp.h"
#include "utils.h"
#if CONFIG_USE_USRSCTP
#include <usrsctp.h>
#endif

static int sctp_diagnostics_enabled = 0;
static void (*sctp_diagnostics_sink)(const char*, int) = NULL;
static FILE* sctp_diag_file = NULL;
static FILE* sctp_diag_trace = NULL;
static FILE* sctp_diag_input = NULL;

static void sctp_close_diagnostics(void) {
  if (sctp_diag_file) fclose(sctp_diag_file);
  if (sctp_diag_trace) fclose(sctp_diag_trace);
  if (sctp_diag_input) fclose(sctp_diag_input);
  sctp_diag_file = NULL;
  sctp_diag_trace = NULL;
  sctp_diag_input = NULL;
}

void sctp_flush_diagnostics(void) {
  if (sctp_diag_file) fflush(sctp_diag_file);
  if (sctp_diag_trace) fflush(sctp_diag_trace);
  if (sctp_diag_input) fflush(sctp_diag_input);
}

void sctp_set_diagnostics_enabled(int enabled) {
  sctp_diagnostics_enabled = enabled != 0;
  if (!sctp_diagnostics_enabled)
    sctp_close_diagnostics();
}

void sctp_set_diagnostics_sink(void (*sink)(const char*, int)) {
  sctp_diagnostics_sink = sink;
  if (sink) sctp_close_diagnostics();
}

static void sctp_diag_log(const char* fmt, ...) {
  if (!sctp_diagnostics_enabled)
    return;
  if (sctp_diagnostics_sink) {
    char line[1024];
    char message[960];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    snprintf(line, sizeof(line), "SCTP %s", message);
    sctp_diagnostics_sink(line, 1);
    return;
  }
  if (!sctp_diag_file)
    sctp_diag_file = fopen("sdmc:/switch/SwitchNOW/signaling.log", "a");
  if (!sctp_diag_trace)
    sctp_diag_trace = fopen("sdmc:/switch/SwitchNOW/stream_trace.log", "a");
  if (!sctp_diag_input)
    sctp_diag_input = fopen("sdmc:/switch/SwitchNOW/input.log", "a");
  FILE* file = sctp_diag_file;
  FILE* trace = sctp_diag_trace;
  FILE* input = sctp_diag_input;
  va_list args;
  va_start(args, fmt);
  if (file) {
    fputs("SCTP ", file);
    va_list copy;
    va_copy(copy, args);
    vfprintf(file, fmt, copy);
    va_end(copy);
    fputc('\n', file);
  }
  if (input) {
    fputs("SCTP ", input);
    va_list copy;
    va_copy(copy, args);
    vfprintf(input, fmt, copy);
    va_end(copy);
    fputc('\n', input);
  }
  if (trace) {
    fputs("SCTP ", trace);
    vfprintf(trace, fmt, args);
    fputc('\n', trace);
  }
  va_end(args);
}

static const char* sctp_assoc_state_name(uint16_t state) {
#if CONFIG_USE_USRSCTP
  switch (state) {
    case SCTP_COMM_UP: return "COMM_UP";
    case SCTP_COMM_LOST: return "COMM_LOST";
    case SCTP_RESTART: return "RESTART";
    case SCTP_SHUTDOWN_COMP: return "SHUTDOWN_COMP";
    case SCTP_CANT_STR_ASSOC: return "CANT_START";
    default: return "UNKNOWN";
  }
#else
  (void)state;
  return "internal";
#endif
}

static const uint32_t crc32c_table[256] = {
    0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
    0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
    0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
    0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
    0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
    0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
    0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
    0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
    0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
    0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
    0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
    0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
    0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
    0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
    0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
    0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
    0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
    0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
    0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
    0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
    0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
    0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
    0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
    0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
    0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
    0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
    0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
    0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
    0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
    0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
    0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
    0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
    0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
    0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
    0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
    0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
    0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
    0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
    0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
    0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
    0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
    0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
    0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
    0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
    0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
    0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
    0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
    0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
    0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
    0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
    0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
    0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
    0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
    0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
    0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
    0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
    0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
    0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
    0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
    0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
    0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
    0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
    0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
    0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L};

uint32_t crc32c(uint32_t crc, const uint8_t* data, unsigned int length) {
  while (length--) {
    crc = crc32c_table[(crc ^ *data++) & 0xFFL] ^ (crc >> 8);
  }
  return crc;
}

static uint32_t sctp_finalize_crc32c(uint32_t crc) {
  uint32_t byte0 = crc & 0x000000ff;
  uint32_t byte1 = (crc >> 8) & 0x000000ff;
  uint32_t byte2 = (crc >> 16) & 0x000000ff;
  uint32_t byte3 = (crc >> 24) & 0x000000ff;
  crc = ((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3);
  return ~crc;
}

static uint32_t sctp_get_checksum(Sctp* sctp, const uint8_t* buf, size_t len) {
  (void)sctp;
  return sctp_finalize_crc32c(crc32c(0xffffffff, buf, len));
}

static int sctp_outgoing_data_cb(void* userdata, void* buf, size_t len, uint8_t tos, uint8_t set_df) {
  Sctp* sctp = (Sctp*)userdata;

  if (buf && len >= 16) {
    const uint8_t chunk_type = ((const uint8_t*)buf)[12];
    uint32_t network_ppid = 0;
    if (len >= 28)
      memcpy(&network_ppid, (const uint8_t*)buf + 24, sizeof(network_ppid));
    if (chunk_type != 0 || (len >= 29 && ntohl(network_ppid) == DATA_CHANNEL_PPID_CONTROL)) {
      sctp_diag_log("tx chunk=%u bytes=%zu connected=%d", chunk_type, len, sctp ? sctp->connected : 0);
    }
  }

  const uint32_t send_begin = ports_get_epoch_time();
  const int written = dtls_srtp_write(sctp->dtls_srtp, buf, len);
  const uint32_t send_elapsed = ports_get_epoch_time() - send_begin;
  if (send_elapsed >= 8) sctp_diag_log("DTLS encrypt_and_send_ms=%u bytes=%zu", send_elapsed, len);
  if (written < 0)
    sctp_diag_log("dtls_write_failed ret=%d bytes=%zu", written, len);
  return written < 0 ? written : 0;
}

void sctp_retransmit_pending(Sctp* sctp) {
#if !CONFIG_USE_USRSCTP
  if (!sctp) return;
  if (!sctp->connected) {
    const uint32_t now = ports_get_epoch_time();
    if (sctp_handshake_due(&sctp->handshake_retry, now, 0)) {
      const int result = dtls_srtp_write(sctp->dtls_srtp,
          sctp->handshake_retry.bytes, sctp->handshake_retry.size);
      sctp->handshake_retry.sent_ms = now;
      ++sctp->handshake_retry.retries;
      sctp_diag_log("handshake_retry chunk=%u attempt=%u result=%d",
          sctp->handshake_retry.bytes[12], sctp->handshake_retry.retries, result);
    }
    return;
  }
  sctp->handshake_retry.size = 0;
  SctpReliableRecord* record = sctp_reliable_oldest(&sctp->reliable_outbound);
  if (!record) return;
  const uint32_t now_ms = ports_get_epoch_time();
  uint32_t rto_ms = sctp->smoothed_rtt_ms ? sctp->smoothed_rtt_ms * 3 : 250;
  if (rto_ms < 200) rto_ms = 200;
  if (rto_ms > 1000 || record->retries >= 6) rto_ms = 1000;
  if (!sctp_reliable_retry_due(record, now_ms, rto_ms)) return;
  // Keep the same TSN and CRC. Resending only the oldest missing packet lets
  // the peer advance its cumulative ACK without flooding the input channel.
  const int result = sctp_outgoing_data_cb(sctp, record->bytes, record->length, 0, 0);
  record->last_attempt_ms = now_ms;
  if (record->retries < UINT8_MAX) ++record->retries;
  if (record->retries <= 3 || record->retries % 8 == 0 || result < 0)
    sctp_diag_log("retransmit tsn=%u attempt=%u outstanding=%u result=%d",
                  record->tsn, record->retries, sctp->reliable_outbound.count, result);
#else
  (void)sctp;
#endif
}

int sctp_outgoing_data(Sctp* sctp, char* buf, size_t len, SctpDataPpid ppid, uint16_t sid) {
#if CONFIG_USE_USRSCTP
  int res;
  struct sctp_sendv_spa spa = {0};

  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;

  spa.sendv_sndinfo.snd_sid = sid;
  spa.sendv_sndinfo.snd_flags = SCTP_EOR;
  spa.sendv_sndinfo.snd_ppid = htonl(ppid);

  res = usrsctp_sendv(sctp->sock, buf, len, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
  if (res < 0) {
    LOGE("sctp sendv error %d: %s", errno, strerror(errno));
  }
  return res;
#else
  const size_t original_len = len;
  size_t padding_len = 0;
  size_t payload_max = SCTP_MTU - sizeof(SctpPacket) - sizeof(SctpDataChunk);
  size_t pos = 0;
  if (sid >= SCTP_MAX_STREAMS || (sctp->outbound_streams && sid >= sctp->outbound_streams)) return -1;

  SctpPacket* packet = (SctpPacket*)(sctp->buf);
  SctpDataChunk* chunk = (SctpDataChunk*)(packet->chunks);

  packet->header.source_port = htons(sctp->local_port);
  packet->header.destination_port = htons(sctp->remote_port);
  packet->header.verification_tag = sctp->verification_tag;

  chunk->type = SCTP_DATA;
  chunk->iube = 0x06;
  chunk->sid = htons(sid);
  chunk->sqn = htons(sctp->outbound_sequence[sid]);
  chunk->ppid = htonl(ppid);

  while (len > payload_max) {
    if (sctp->reliable_outbound.count >= SCTP_RELIABLE_CAPACITY) {
      sctp_diag_log("send_window_full outstanding=%u", sctp->reliable_outbound.count);
      return SCTP_SEND_WINDOW_FULL;
    }
    chunk->length = htons(payload_max + sizeof(SctpDataChunk));
    chunk->tsn = htonl(sctp->tsn);
    memcpy(chunk->data, buf + pos, payload_max);
    packet->header.checksum = 0;

    packet->header.checksum = htonl(sctp_get_checksum(sctp, (const uint8_t*)sctp->buf, SCTP_MTU));

    if (sctp_outgoing_data_cb(sctp, sctp->buf, SCTP_MTU, 0, 0) < 0)
      return -1;
    (void)sctp_reliable_push(&sctp->reliable_outbound,
                             (const uint8_t*)sctp->buf, SCTP_MTU,
                             sctp->tsn, ports_get_epoch_time());
    ++sctp->tsn;
    sctp->last_outbound_tsn = ntohl(chunk->tsn);
    sctp->last_outbound_sent_ms = ports_get_epoch_time();
    chunk->iube = 0x04;
    len -= payload_max;
    pos += payload_max;
  }

  if (len > 0) {
    if (sctp->reliable_outbound.count >= SCTP_RELIABLE_CAPACITY) {
      sctp_diag_log("send_window_full outstanding=%u", sctp->reliable_outbound.count);
      return SCTP_SEND_WINDOW_FULL;
    }
    chunk->length = htons(len + sizeof(SctpDataChunk));
    chunk->iube++;
    chunk->tsn = htonl(sctp->tsn);
    memset(chunk->data, 0, payload_max);
    memcpy(chunk->data, buf + pos, len);
    packet->header.checksum = 0;

    padding_len = 4 * ((len + sizeof(SctpDataChunk) + sizeof(SctpPacket) + 3) / 4);

    packet->header.checksum = htonl(sctp_get_checksum(sctp, (const uint8_t*)sctp->buf, padding_len));

    if (sctp_outgoing_data_cb(sctp, sctp->buf, padding_len, 0, 0) < 0)
      return -1;
    (void)sctp_reliable_push(&sctp->reliable_outbound,
                             (const uint8_t*)sctp->buf, padding_len,
                             sctp->tsn, ports_get_epoch_time());
    ++sctp->tsn;
    sctp->last_outbound_tsn = ntohl(chunk->tsn);
    sctp->last_outbound_sent_ms = ports_get_epoch_time();
  }
  ++sctp->outbound_sequence[sid];
  return (int)original_len;
#endif
}

void sctp_add_stream_mapping(Sctp* sctp, const char* label, uint16_t sid) {
  if (sctp->stream_count < SCTP_MAX_STREAMS) {
    strncpy(sctp->stream_table[sctp->stream_count].label, label, sizeof(sctp->stream_table[sctp->stream_count].label));
    sctp->stream_table[sctp->stream_count].sid = sid;
    sctp->stream_count++;
  } else
    LOGE("Stream table full. Cannot add more streams.");
}

void sctp_parse_data_channel_open(Sctp* sctp, uint16_t sid, char* data, size_t length) {
  if (length < 12)
    return;  // Not enough data for a DATA_CHANNEL_OPEN message

  if (data[0] == DATA_CHANNEL_OPEN) {
    uint16_t label_length = ntohs(*(uint16_t*)(data + 8));
    uint16_t protocol_length = ntohs(*(uint16_t*)(data + 10));

    // Ensure we have enough data for the label and protocol
    if (length < 12 + label_length + protocol_length)
      return;

    char* label = (char*)(data + 12);

    // copy and null-terminate
    char label_str[label_length + 1];
    memcpy(label_str, label, label_length);
    label_str[label_length] = '\0';

    // Log or process the DATA_CHANNEL_OPEN message
    printf("DATA_CHANNEL_OPEN: Label=%s, sid=%d\n", label_str, sid);

    // Add stream mapping
    sctp_add_stream_mapping(sctp, label_str, sid);
    sctp_diag_log("dcep_open sid=%u label=%s", sid, label_str);
    char ack = DATA_CHANNEL_ACK;
    sctp_outgoing_data(sctp, &ack, 1, PPID_CONTROL, sid);
  }
}

void sctp_handle_sctp_packet(Sctp* sctp, char* buf, size_t len) {
  if (len <= 29)
    return;

  if (buf[12] != 0)  // if chunk_type is no zero, it's not data
    return;

  uint16_t sid = ntohs(*(uint16_t*)(buf + 20));
  uint32_t ppid = ntohl(*(uint32_t*)(buf + 24));

  if (ppid == DATA_CHANNEL_PPID_CONTROL)
    sctp_parse_data_channel_open(sctp, sid, buf + 28, len - 28);
}

void sctp_incoming_data(Sctp* sctp, char* buf, size_t len) {
  if (!sctp)
    return;

#if CONFIG_USE_USRSCTP
  if (len >= 16) {
    const uint8_t chunk_type = (uint8_t)buf[12];
    uint32_t network_ppid = 0;
    if (len >= 28)
      memcpy(&network_ppid, (const uint8_t*)buf + 24, sizeof(network_ppid));
    if (chunk_type != 0 || (len >= 29 && ntohl(network_ppid) == DATA_CHANNEL_PPID_CONTROL)) {
      sctp_diag_log("rx chunk=%u bytes=%zu connected=%d", chunk_type, len, sctp->connected);
    }
  }
  usrsctp_conninput(sctp, buf, len, 0);
#else
  if (!buf || len < sizeof(SctpHeader) + sizeof(SctpChunkCommon) || len > CONFIG_MTU)
    return;
  size_t length = 0;
  int outgoing_dcep_ack = 0;
  size_t pos = sizeof(SctpHeader);
  SctpChunkCommon* chunk_common;
  SctpPacket* in_packet = (SctpPacket*)buf;
  SctpPacket* out_packet = (SctpPacket*)sctp->buf;

  // Header
#if 0
  LOGD("source_port %d", ntohs(in_packet->header.source_port));
  LOGD("destination_port %d", ntohs(in_packet->header.destination_port));
  LOGD("verification_tag %ld", ntohl(in_packet->header.verification_tag));
  LOGD("checksum %d", ntohs(in_packet->header.checksum));
#endif
  const uint32_t received_crc32c = ntohl(in_packet->header.checksum);

  in_packet->header.checksum = 0;
  const uint32_t calculated_crc32c = sctp_get_checksum(sctp, (const uint8_t*)buf, len);
  const int crc_ok = received_crc32c == calculated_crc32c;
  const uint8_t first_chunk_type = len > sizeof(SctpHeader) ? (uint8_t)buf[sizeof(SctpHeader)] : 0xff;
  static uint32_t internal_rx_diag_count = 0;
  internal_rx_diag_count++;
  if (!crc_ok || internal_rx_diag_count <= 10 || internal_rx_diag_count % 600 == 0) {
    sctp_diag_log("internal_rx count=%u chunk=%u bytes=%zu crcOk=%d receivedCrc=%08x calculatedCrc=%08x",
                  internal_rx_diag_count, first_chunk_type, len, crc_ok,
                  received_crc32c, calculated_crc32c);
  }

  if (!crc_ok) {
    LOGE("checksum error");
    return;
  }

  const uint32_t tag = ntohl(in_packet->header.verification_tag);
  if ((first_chunk_type == SCTP_INIT && tag != 0) ||
      (first_chunk_type != SCTP_INIT && tag != sctp->local_verification_tag)) {
    sctp_diag_log("handshake_drop reason=verification_tag chunk=%u", first_chunk_type);
    return;
  }

  // prepare outgoing packet
  memset(sctp->buf, 0, sizeof(sctp->buf));
  while (pos + sizeof(SctpChunkCommon) <= len) {
    chunk_common = (SctpChunkCommon*)(buf + pos);
    const size_t chunk_length = ntohs(chunk_common->length);
    if (chunk_length < sizeof(SctpChunkCommon) || chunk_length > len - pos) {
      sctp_diag_log("handshake_drop reason=chunk_length chunk=%u", chunk_common->type);
      return;
    }
    if (((chunk_common->type == SCTP_INIT || chunk_common->type == SCTP_INIT_ACK) && chunk_length < 20) ||
        (chunk_common->type == SCTP_DATA && chunk_length < sizeof(SctpDataChunk) + 1) ||
        (chunk_common->type == SCTP_SACK && chunk_length < 16)) return;
    length = 0;

    switch (chunk_common->type) {
      case SCTP_DATA: {
        if (!sctp->connected) break;
        SctpDataChunk* data_chunk = (SctpDataChunk*)(buf + pos);
        SctpSackChunk* sack_chunk = (SctpSackChunk*)out_packet->chunks;

        sack_chunk->common.type = SCTP_SACK;
        sack_chunk->common.flags = 0x00;
        sack_chunk->common.length = htons(16);
        sack_chunk->cumulative_tsn_ack = data_chunk->tsn;
        // Advertise a realistic receive window so the peer does not throttle
        // the control channel after the first DataChannel message.
        sack_chunk->a_rwnd = htonl(0x100000);
        length = ntohs(sack_chunk->common.length) + sizeof(SctpHeader);

        LOGD("SCTP_DATA. ppid = %ld, data = %.2x", ntohl(data_chunk->ppid), data_chunk->data[0]);
        if (ntohl(data_chunk->ppid) == DATA_CHANNEL_PPID_CONTROL && data_chunk->data[0] == DATA_CHANNEL_OPEN) {
          if (sctp->reliable_outbound.count >= SCTP_RELIABLE_CAPACITY) {
            sctp_diag_log("dcep_ack_window_full outstanding=%u", sctp->reliable_outbound.count);
            pos = len;
            break;
          }
          data_chunk = (SctpDataChunk*)sack_chunk->blocks;
          data_chunk->type = SCTP_DATA;
          data_chunk->iube = 0x03;
          data_chunk->tsn = htonl(sctp->tsn);
          data_chunk->sid = htons(0);
          data_chunk->sqn = htons(0);
          data_chunk->ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
          data_chunk->length = htons(1 + sizeof(SctpDataChunk));
          data_chunk->data[0] = DATA_CHANNEL_ACK;
          length += ntohs(data_chunk->length);
          outgoing_dcep_ack = 1;
        } else if (ntohl(data_chunk->ppid) == DATA_CHANNEL_PPID_DOMSTRING || ntohl(data_chunk->ppid) == DATA_CHANNEL_PPID_BINARY) {
          if (sctp->onmessage) {
            sctp->onmessage((char*)data_chunk->data, ntohs(data_chunk->length) - sizeof(SctpDataChunk),
                            sctp->userdata, ntohs(data_chunk->sid));
          }
        }
        pos = len;  // Do not handle other msg
      } break;
      case SCTP_INIT: {
        LOGD("SCTP_INIT");

        SctpInitChunk* init_chunk;
        init_chunk = (SctpInitChunk*)chunk_common;
        const uint16_t peer_out = ntohs(init_chunk->number_of_outbound_streams);
        const uint16_t peer_in = ntohs(init_chunk->number_of_inbound_streams);
        if (!init_chunk->initiate_tag || !peer_out || !peer_in || ntohl(init_chunk->a_rwnd) < 1500)
          return;
        if (sctp->connected && sctp->verification_tag != init_chunk->initiate_tag) return;
        sctp->verification_tag = init_chunk->initiate_tag;
        sctp->outbound_streams = peer_in < SCTP_MAX_STREAMS ? peer_in : SCTP_MAX_STREAMS;

        SctpInitChunk* init_ack = (SctpInitChunk*)out_packet->chunks;
        init_ack->common.type = SCTP_INIT_ACK;
        init_ack->common.flags = 0x00;
        init_ack->common.length = htons(20 + 4 + sizeof(sctp->local_cookie));
        init_ack->initiate_tag = htonl(sctp->local_verification_tag);
        init_ack->a_rwnd = htonl(0x100000);
        // RFC9260 3.3.3: OS in INIT_ACK cannot exceed the peer's INIT MIS.
        init_ack->number_of_outbound_streams = htons(sctp->outbound_streams);
        init_ack->number_of_inbound_streams = htons(sctp->inbound_streams);
        init_ack->initial_tsn = htonl(sctp->initial_tsn);

        SctpChunkParam* param = init_ack->param;

        param->type = htons(SCTP_PARAM_STATE_COOKIE);
        param->length = htons(4 + sizeof(sctp->local_cookie));
        memcpy(param->value, sctp->local_cookie, sizeof(sctp->local_cookie));
        sctp->cookie_issued = 1;
        length = ntohs(init_ack->common.length) + sizeof(SctpHeader);
        sctp_diag_log("internal_init_ack prepared peerTag=%08x localTag=%08x peerOS=%u peerMIS=%u negotiatedOS=%u localMIS=%u bytes=%zu",
                      ntohl(sctp->verification_tag), sctp->local_verification_tag,
                      peer_out, peer_in, sctp->outbound_streams, sctp->inbound_streams, length);
      } break;
      case SCTP_INIT_ACK: {
        if (sctp->connected) break;
        SctpInitChunk* init_ack = (SctpInitChunk*)chunk_common;
        SctpCookieEchoChunk* cookie_echo = (SctpCookieEchoChunk*)out_packet->chunks;
        SctpChunkParam* param = NULL;
        const uint16_t peer_out = ntohs(init_ack->number_of_outbound_streams);
        const uint16_t peer_in = ntohs(init_ack->number_of_inbound_streams);
        if (!init_ack->initiate_tag || !peer_out || !peer_in ||
            peer_out > sctp->inbound_streams || ntohl(init_ack->a_rwnd) < 1500) return;
        const int init_ack_length = ntohs(init_ack->common.length);
        uint8_t* params = (uint8_t*)&init_ack->param[0];
        int offset = 0;
        while (offset + 4 <= init_ack_length - 20) {
          SctpChunkParam* candidate = (SctpChunkParam*)(params + offset);
          const uint16_t candidate_type = ntohs(candidate->type);
          const uint16_t candidate_length = ntohs(candidate->length);
          if (candidate_length < 4 || offset + candidate_length > init_ack_length - 20)
            break;
          if (candidate_type == SCTP_PARAM_STATE_COOKIE) {
            param = candidate;
            break;
          }
          offset += (candidate_length + 3) & ~3;
        }
        if (!param || ntohs(param->length) <= 4 ||
            (size_t)ntohs(param->length) + sizeof(SctpHeader) > sizeof(sctp->buf)) {
          LOGE("SCTP_INIT_ACK without cookie");
          break;
        }
        sctp->verification_tag = init_ack->initiate_tag;
        sctp->outbound_streams = peer_in < SCTP_MAX_STREAMS ? peer_in : SCTP_MAX_STREAMS;
        sctp->cookie_echo_pending = 1;

        cookie_echo->common.type = SCTP_COOKIE_ECHO;
        cookie_echo->common.flags = 0x00;
        // cookie echo: type + flag + length (4 bytes) + cookie
        cookie_echo->common.length = htons(ntohs(param->length));
        // param: type + length (4 bytes) + cookie
        memcpy(cookie_echo->cookie, param->value, ntohs(param->length) - 4);
        length = ntohs(cookie_echo->common.length) + sizeof(SctpHeader);
        sctp_diag_log("internal_cookie_echo prepared peerTag=%08x cookieBytes=%u",
                      ntohl(sctp->verification_tag), ntohs(param->length) - 4);
      } break;
      case SCTP_SACK: {
        SctpSackChunk* sack = (SctpSackChunk*)(buf + pos);
        if (ntohs(sack->common.length) >= 16) {
          SctpReliableRecord* oldest = sctp_reliable_oldest(&sctp->reliable_outbound);
          const uint32_t cumulative_tsn = ntohl(sack->cumulative_tsn_ack);
          if (oldest && oldest->retries == 0 &&
              (int32_t)(cumulative_tsn - oldest->tsn) >= 0) {
            const uint32_t sample_ms = ports_get_epoch_time() - oldest->last_attempt_ms;
            if (sample_ms > 0 && sample_ms <= 5000)
              sctp->smoothed_rtt_ms = sctp->smoothed_rtt_ms == 0
                  ? sample_ms : (sctp->smoothed_rtt_ms * 7 + sample_ms) / 8;
          }
          const unsigned acknowledged =
              sctp_reliable_ack(&sctp->reliable_outbound, cumulative_tsn);
          if (acknowledged >= 8)
            sctp_diag_log("ack_progress cumulativeTsn=%u released=%u outstanding=%u",
                          cumulative_tsn, acknowledged, sctp->reliable_outbound.count);
        }
        // Only the never-retransmitted reliable record is a valid RTT sample.
        // The legacy last_outbound sample measured the same SACK twice and
        // could include a retransmission (Karn's ambiguity).
#if 0
        LOGD("SCTP_SACK");
        LOGD("cumulative_tsn_ack %d", ntohl(sack->cumulative_tsn_ack));
        LOGD("a_rwnd %d", ntohl(sack->a_rwnd));
        LOGD("number_of_gap_ack_blocks %d", sack->number_of_gap_ack_blocks);
        LOGD("number_of_dup_tsns %d", sack->number_of_dup_tsns);
#endif
// XXX: unordered sequence
#if 0
        if (sack->number_of_gap_ack_blocks > 0) {

          int blocks = ntohs(sack->number_of_gap_ack_blocks);
          LOGW("cumulative_tsn_ack: %ld, number_of_gap_ack_blocks: %d",
           ntohl(sack->cumulative_tsn_ack), blocks);

          for (int i = 0; i < blocks; i++) {

            uint16_t *start = (uint16_t*)sack->blocks + i*2;
            uint16_t *end = (uint16_t*)sack->blocks + i*2 + 1;
            LOGW("start: %d, end: %d", ntohs(*start), ntohs(*end));
            sctp->tsn = ntohl(sack->cumulative_tsn_ack) + 1;// + (*start) - 1;
          }
        } else if (sack->number_of_dup_tsns > 0) {

          LOGW("cumulative_tsn_ack: %ld, number_of_dup_tsns: %d",
           ntohl(sack->cumulative_tsn_ack),
           ntohs(sack->number_of_dup_tsns));
        }
#endif
      } break;
      case SCTP_COOKIE_ECHO: {
        if (!sctp->cookie_issued || chunk_length != 4 + sizeof(sctp->local_cookie) ||
            memcmp(buf + pos + 4, sctp->local_cookie, sizeof(sctp->local_cookie)) != 0) {
          sctp_diag_log("handshake_drop reason=cookie_mismatch");
          return;
        }
        LOGD("SCTP_COOKIE_ECHO");
        SctpChunkCommon* common = (SctpChunkCommon*)out_packet->chunks;
        common->type = SCTP_COOKIE_ACK;
        common->length = htons(4);
        length = ntohs(common->length) + sizeof(SctpHeader);
        sctp->handshake_retry.size = 0;
        if (!sctp->connected) {
          sctp->connected = 1;
          sctp_diag_log("internal_assoc connected_by=cookie_echo");
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }
        }
      } break;
      case SCTP_COOKIE_ACK: {
        if (!sctp->cookie_echo_pending || chunk_length != 4) break;
        sctp->handshake_retry.size = 0;
        if (!sctp->connected) {
          sctp->connected = 1;
          sctp_diag_log("internal_assoc connected_by=cookie_ack");
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }
        }
        break;
      }
      case SCTP_ABORT:
        sctp->handshake_retry.size = 0;
        sctp->connected = 0;
        if (sctp->onclose) {
          sctp->onclose(sctp->userdata);
        }
        break;
      default:
        LOGI("Unknown chunk type %d", chunk_common->type);
        length = 0;
        break;
    }

    out_packet->header.source_port = htons(sctp->local_port);
    out_packet->header.destination_port = htons(sctp->remote_port);
    out_packet->header.verification_tag = sctp->verification_tag;
    out_packet->header.checksum = 0x00;

    if (length > 0) {
      // padding 4
      length = (4 * ((length + 3) / 4));
      const uint32_t calculated_checksum = sctp_get_checksum(sctp, sctp->buf, length);
      out_packet->header.checksum = htonl(calculated_checksum);
      const int written = dtls_srtp_write(sctp->dtls_srtp, sctp->buf, length);
      if (!sctp->connected && out_packet->chunks[0] == SCTP_COOKIE_ECHO)
        sctp_handshake_save(&sctp->handshake_retry, sctp->buf, length,
                            ports_get_epoch_time());
      if (outgoing_dcep_ack && written >= 0) {
        (void)sctp_reliable_push(&sctp->reliable_outbound, sctp->buf, length,
                                 sctp->tsn, ports_get_epoch_time());
        ++sctp->tsn;
      }
      static uint32_t internal_tx_diag_count = 0;
      internal_tx_diag_count++;
      if (written < 0 || internal_tx_diag_count <= 10 || internal_tx_diag_count % 600 == 0) {
        sctp_diag_log("internal_tx count=%u chunk=%u bytes=%zu dtlsWrite=%d crc=%08x",
                      internal_tx_diag_count, (uint8_t)out_packet->chunks[0], length,
                      written, calculated_checksum);
      }
      // sctp_outgoing_data_cb(sctp, sctp->buf, SCTP_MTU, 0, 0);
    }
    pos += (chunk_length + 3u) & ~3u;
  }
#endif
}

static int sctp_handle_incoming_data(Sctp* sctp, char* data, size_t len, uint32_t ppid, uint16_t sid, int flags) {
#if CONFIG_USE_USRSCTP
  switch (ppid) {
    case DATA_CHANNEL_PPID_CONTROL:
      if (data && len > 0) {
        if ((uint8_t)data[0] == DATA_CHANNEL_OPEN)
          sctp_parse_data_channel_open(sctp, sid, data, len);
        else if ((uint8_t)data[0] == DATA_CHANNEL_ACK)
          sctp_diag_log("dcep_ack sid=%u", sid);
        else
          sctp_diag_log("dcep_control sid=%u type=%u bytes=%zu", sid, (uint8_t)data[0], len);
      }
      break;

    case DATA_CHANNEL_PPID_DOMSTRING:
    case DATA_CHANNEL_PPID_BINARY:
    case DATA_CHANNEL_PPID_DOMSTRING_PARTIAL:
    case DATA_CHANNEL_PPID_BINARY_PARTIAL:

      LOGD("Got message (size = %ld)", len);
      if (sctp->onmessage) {
        sctp->onmessage(data, len, sctp->userdata, sid);
      }
      break;

    default:
      break;
  }
#endif
  return 0;
}

#if CONFIG_USE_USRSCTP

static void sctp_process_notification(Sctp* sctp, union sctp_notification* notification, size_t len) {
  if (notification->sn_header.sn_length != (uint32_t)len) {
    return;
  }

  switch (notification->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE:
      sctp_diag_log("assoc state=%s(%u) error=%u out=%u in=%u",
                    sctp_assoc_state_name(notification->sn_assoc_change.sac_state),
                    notification->sn_assoc_change.sac_state,
                    notification->sn_assoc_change.sac_error,
                    notification->sn_assoc_change.sac_outbound_streams,
                    notification->sn_assoc_change.sac_inbound_streams);

      switch (notification->sn_assoc_change.sac_state) {
        case SCTP_COMM_UP:

          sctp->connected = 1;
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }

          break;

        case SCTP_COMM_LOST:
        case SCTP_SHUTDOWN_COMP:
          sctp->connected = 0;
          if (sctp->onclose) {
            sctp->onclose(sctp->userdata);
          }
        default:
          break;
      }
      break;
    default:
      sctp_diag_log("notification type=%u bytes=%zu", notification->sn_header.sn_type, len);
      break;
  }
}

static int sctp_incoming_data_cb(struct socket* sock, union sctp_sockstore addr, void* data, size_t len, struct sctp_rcvinfo recv_info, int flags, void* userdata) {
  Sctp* sctp = (Sctp*)userdata;
  LOGD("Data of length %u received on stream %u with SSN %u, TSN %u, PPID %u",
       (uint32_t)len,
       recv_info.rcv_sid,
       recv_info.rcv_ssn,
       recv_info.rcv_tsn,
       ntohl(recv_info.rcv_ppid));
  if (flags & MSG_NOTIFICATION) {
    sctp_process_notification(sctp, (union sctp_notification*)data, len);
  } else {
    sctp_handle_incoming_data(sctp, data, len, ntohl(recv_info.rcv_ppid), recv_info.rcv_sid, flags);
  }
  free(data);  // we need to free the memory that usrsctp allocates
  return 0;
}
#endif

void sctp_usrsctp_init() {
#if CONFIG_USE_USRSCTP
  usrsctp_init(0, sctp_outgoing_data_cb, NULL);
#endif
}

void sctp_usrsctp_deinit() {
#if CONFIG_USE_USRSCTP
  for (int attempt = 0; attempt < 20; ++attempt) {
    const int ret = usrsctp_finish();
    if (ret == 0) {
      sctp_diag_log("shutdown_complete attempts=%d", attempt + 1);
      return;
    }
    usleep(10000);
  }
  sctp_diag_log("shutdown_timeout");
#endif
}

int sctp_create_association(Sctp* sctp, DtlsSrtp* dtls_srtp) {
  sctp->dtls_srtp = dtls_srtp;
  sctp->local_port = 5000;
  sctp->remote_port = 5000;
  sctp->verification_tag = 0;
  sctp->tsn = 1234;
  sctp->initial_tsn = sctp->tsn;
  sctp->outbound_streams = sctp->inbound_streams = SCTP_MAX_STREAMS;
  sctp->cookie_issued = sctp->cookie_echo_pending = sctp->connected = 0;
  memset(&sctp->reliable_outbound, 0, sizeof(sctp->reliable_outbound));
  memset(&sctp->handshake_retry, 0, sizeof(sctp->handshake_retry));
  memset(sctp->outbound_sequence, 0, sizeof(sctp->outbound_sequence));
#if CONFIG_USE_USRSCTP
  int ret = -1;
  usrsctp_sysctl_set_sctp_ecn_enable(0);
  usrsctp_register_address(sctp);

  struct socket* sock = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP,
                                       sctp_incoming_data_cb, NULL, 0, sctp);

  if (!sock) {
    LOGE("usrsctp_socket failed");
    usrsctp_deregister_address(sctp);
    return -1;
  }
  sctp->sock = sock;

  do {
    if (usrsctp_set_non_blocking(sock, 1) < 0) {
      LOGE("usrsctp_set_non_blocking failed");
      break;
    }

    struct linger lopt;
    lopt.l_onoff = 1;
    lopt.l_linger = 0;
    usrsctp_setsockopt(sock, SOL_SOCKET, SO_LINGER, &lopt, sizeof(lopt));

#if 0
    struct sctp_paddrparams peer_param;
    memset(&peer_param, 0, sizeof peer_param);
    peer_param.spp_flags = SPP_PMTUD_DISABLE;
    peer_param.spp_pathmtu = 1200;
    usrsctp_setsockopt(s, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &peer_param, sizeof peer_param);
#endif

    struct sctp_assoc_value av;
    av.assoc_id = SCTP_ALL_ASSOC;
    av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av, sizeof(av));

    uint32_t nodelay = 1;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_NODELAY, &nodelay, sizeof(nodelay));

    static uint16_t event_types[] = {
        SCTP_ASSOC_CHANGE,
        SCTP_PEER_ADDR_CHANGE,
        SCTP_REMOTE_ERROR,
        SCTP_SHUTDOWN_EVENT,
        SCTP_ADAPTATION_INDICATION,
        SCTP_SEND_FAILED_EVENT,
        SCTP_SENDER_DRY_EVENT,
        SCTP_STREAM_RESET_EVENT,
        SCTP_STREAM_CHANGE_EVENT};

    struct sctp_event event;
    memset(&event, 0, sizeof(event));
    event.se_assoc_id = SCTP_ALL_ASSOC;
    event.se_on = 1;
    for (int i = 0; i < sizeof(event_types) / sizeof(uint16_t); i++) {
      event.se_type = event_types[i];
      usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
    }

    struct sctp_initmsg init_msg;
    memset(&init_msg, 0, sizeof init_msg);
    init_msg.sinit_num_ostreams = 300;
    init_msg.sinit_max_instreams = 300;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_INITMSG, &init_msg, sizeof init_msg);

    struct sockaddr_conn sconn;
    memset(&sconn, 0, sizeof(sconn));
    sconn.sconn_family = AF_CONN;
    sconn.sconn_port = htons(sctp->local_port);
    sconn.sconn_addr = (void*)sctp;
    ret = usrsctp_bind(sock, (struct sockaddr*)&sconn, sizeof(sconn));
    sctp_diag_log("bind ret=%d errno=%d localPort=%d", ret, ret < 0 ? errno : 0, sctp->local_port);
    if (ret < 0)
      break;

    struct sockaddr_conn rconn;

    memset(&rconn, 0, sizeof(struct sockaddr_conn));
    rconn.sconn_family = AF_CONN;
    rconn.sconn_port = htons(sctp->remote_port);
    rconn.sconn_addr = (void*)sctp;
    ret = usrsctp_connect(sock, (struct sockaddr*)&rconn, sizeof(struct sockaddr_conn));
    sctp_diag_log("connect ret=%d errno=%d remotePort=%d", ret, ret < 0 ? errno : 0, sctp->remote_port);

    if (ret < 0 && errno != EINPROGRESS) {
      LOGE("connect error");
      break;
    }

    ret = 0;

  } while (0);

  if (ret < 0) {
    sctp_destroy_association(sctp);
    return -1;
  }

#else
  uint32_t random_tag = 0;
  if (mbedtls_ctr_drbg_random(&dtls_srtp->ctr_drbg, (unsigned char*)&random_tag, sizeof(random_tag)) != 0 ||
      mbedtls_ctr_drbg_random(&dtls_srtp->ctr_drbg, sctp->local_cookie, sizeof(sctp->local_cookie)) != 0)
    return -1;
  sctp->local_verification_tag = random_tag ? random_tag : 1u;
  // send SCTP_INIT
  int length = 0;
  SctpInitChunk* init_chunk;
  SctpHeader* header;
  SctpPacket* out_packet = (SctpPacket*)sctp->buf;
  header = &out_packet->header;
  init_chunk = (SctpInitChunk*)out_packet->chunks;

  header->source_port = htons(sctp->local_port);
  header->destination_port = htons(sctp->remote_port);
  header->verification_tag = 0x0;
  init_chunk->common.type = SCTP_INIT;
  init_chunk->common.flags = 0x00;
  init_chunk->common.length = htons(20);
  init_chunk->initiate_tag = htonl(sctp->local_verification_tag);
  init_chunk->a_rwnd = htonl(0x100000);
  init_chunk->number_of_outbound_streams = htons(sctp->outbound_streams);
  init_chunk->number_of_inbound_streams = htons(sctp->inbound_streams);
  init_chunk->initial_tsn = htonl(sctp->tsn);
  length = ntohs(init_chunk->common.length) + sizeof(SctpHeader);
  length = (4 * ((length + 3) / 4));
  // The same buffer is reused for retries. SCTP checksums are calculated with
  // this field cleared, otherwise every retry after the first has a bad CRC.
  header->checksum = 0;
  const uint32_t calculated_checksum = sctp_get_checksum(sctp, sctp->buf, length);
  header->checksum = htonl(calculated_checksum);
  const int written = dtls_srtp_write(sctp->dtls_srtp, sctp->buf, length);
  sctp_handshake_save(&sctp->handshake_retry, sctp->buf, length,
                      ports_get_epoch_time());
  sctp_diag_log("internal_init tx bytes=%d dtlsWrite=%d crc=%08x",
                length, written, calculated_checksum);
#endif

  return 0;
}

void sctp_destroy_association(Sctp* sctp) {
  if (sctp) sctp->handshake_retry.size = 0;
#if CONFIG_USE_USRSCTP
  if (sctp && sctp->sock) {
    usrsctp_shutdown(sctp->sock, SHUT_RDWR);
    usrsctp_close(sctp->sock);
    sctp->sock = NULL;
    usrsctp_deregister_address(sctp);
  }
#endif
}

int sctp_is_connected(Sctp* sctp) {
  return sctp->connected;
}

int sctp_get_rtt_ms(Sctp* sctp) {
#if CONFIG_USE_USRSCTP
  if (!sctp || !sctp->sock || !sctp->connected)
    return -1;

  struct sctp_status status;
  socklen_t status_len = sizeof(status);
  memset(&status, 0, sizeof(status));
  if (usrsctp_getsockopt(
          sctp->sock, IPPROTO_SCTP, SCTP_STATUS, &status, &status_len) < 0) {
    return -1;
  }

  return status.sstat_primary.spinfo_srtt > 0
      ? (int)status.sstat_primary.spinfo_srtt
      : -1;
#else
  return sctp && sctp->smoothed_rtt_ms > 0
      ? (int)sctp->smoothed_rtt_ms
      : -1;
#endif
}

void sctp_onmessage(Sctp* sctp, void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid)) {
  sctp->onmessage = onmessage;
}

void sctp_onopen(Sctp* sctp, void (*onopen)(void* userdata)) {
  sctp->onopen = onopen;
}

void sctp_onclose(Sctp* sctp, void (*onclose)(void* userdata)) {
  sctp->onclose = onclose;
}
