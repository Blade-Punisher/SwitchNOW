#include "socket.h"
#include "udp_receive_batch.h"
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

static void exercise(int family) {
  UdpSocket receiver = {.fd = -1}, sender = {.fd = -1};
  assert(udp_socket_open(&receiver, family, 0) == 0);
  assert(udp_socket_open(&sender, family, 0) == 0);
  assert((fcntl(receiver.fd, F_GETFL, 0) & O_NONBLOCK) != 0);
#if defined(_GNU_SOURCE)
  assert(udp_socket_enable_receive_batch(&receiver));
#endif
  Address target = receiver.bind_addr;
  if (family == AF_INET) target.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  else target.sin6.sin6_addr = in6addr_loopback;
  uint8_t buffer[16];
  // Empty socket and a consumed readiness notification must never block.
  assert(udp_socket_recvfrom(&receiver, NULL, buffer, sizeof(buffer)) == 0);
  for (uint8_t i = 0; i < 64; ++i) {
    assert(udp_socket_sendto(&sender, &target, &i, 1) == 1);
  }
  for (uint8_t i = 0; i < 64; ++i) {
    struct pollfd ready = {.fd = receiver.fd, .events = POLLIN};
    if (!receiver.receive_batch || receiver.receive_batch->next >= receiver.receive_batch->count)
      assert(poll(&ready, 1, 1000) == 1);
    Address source = {0};
    assert(udp_socket_recvfrom(&receiver, &source, buffer, sizeof(buffer)) == 1);
    assert(buffer[0] == i && source.family == family);
  }
  assert(udp_socket_recvfrom(&receiver, NULL, buffer, sizeof(buffer)) == 0);
  if(receiver.receive_batch) {
    assert(receiver.receive_batch->datagrams==63); // first datagram primes batching
    assert(receiver.receive_batch->cache_hits>=55);
  }
  udp_socket_close(&receiver);
  udp_socket_close(&sender);
  assert(receiver.fd == -1 && sender.fd == -1);
  assert(receiver.receive_batch==NULL);
}

int main(void) {
  // Turn an accidental blocking regression into a bounded test failure.
  alarm(5);
  exercise(AF_INET);
  exercise(AF_INET6);
  alarm(0);
  return 0;
}
