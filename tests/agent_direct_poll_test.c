#include <assert.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
static unsigned select_calls;
static int counted_select(int count, fd_set* read, fd_set* write, fd_set* except, struct timeval* timeout) {
  ++select_calls;
  return select(count, read, write, except, timeout);
}
#define select counted_select
// Exercise the actual private receive routine, retaining only its dependency
// sections at link time. No ICE/STUN service or remote network is required.
#include "agent.c"
#undef select
uint32_t ports_get_epoch_time(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint32_t)(now.tv_sec * 1000u + now.tv_nsec / 1000000u);
}
int main(void) {
  alarm(5);
  Agent agent = {0};
  UdpSocket sender = {.fd=-1};
  assert(udp_socket_open(&agent.udp_sockets[0],AF_INET,0) == 0);
  assert(udp_socket_open(&sender,AF_INET,0) == 0);
#if defined(_GNU_SOURCE)
  assert(udp_socket_enable_receive_batch(&agent.udp_sockets[0]));
#endif
  Address target=agent.udp_sockets[0].bind_addr;
  target.sin.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  uint8_t data[16];
  assert(agent_socket_recv_timeout(&agent,NULL,data,sizeof(data),0) == 0);
  assert(select_calls == 0);
  for (unsigned i=0;i<32;++i) {
    const uint8_t value=(uint8_t)i;
    assert(udp_socket_sendto(&sender,&target,&value,1) == 1);
  }
  for (unsigned i=0;i<32;++i) {
    int received=0;
    for (unsigned tries=0;tries<1000 && received==0;++tries)
      received=agent_socket_recv_timeout(&agent,NULL,data,sizeof(data),
          agent.udp_sockets[0].receive_batch &&
          agent.udp_sockets[0].receive_batch->next < agent.udp_sockets[0].receive_batch->count ? 1 : 0);
    assert(received == 1 && data[0] == i);
  }
  assert(select_calls == 0);
  assert(agent_socket_recv_timeout(&agent,NULL,data,sizeof(data),0) == 0);
  assert(agent_socket_recv_timeout(&agent,NULL,data,sizeof(data),1) == 0);
  assert(select_calls == 1); // Handshake paths retain their timed polling.
  udp_socket_close(&agent.udp_sockets[0]); udp_socket_close(&sender);
  alarm(0);
}
