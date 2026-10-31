#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include "udp_receive_batch.h"
#if defined(__SWITCH__)
// libnx defines msghdr/mmsghdr/iovec in sys/socket.h (no sys/uio.h).
#define UDP_HAS_RECVMMSG 1
#elif defined(__linux__) && defined(_GNU_SOURCE)
#include <sys/uio.h>
#define UDP_HAS_RECVMMSG 1
#endif

#include "socket.h"
#include "utils.h"
_Static_assert(UDP_RECEIVE_BATCH_BYTES >= CONFIG_MTU, "UDP batch must fit a complete transport datagram");

int udp_socket_add_multicast_group(UdpSocket* udp_socket, Address* mcast_addr) {
  int ret = 0;
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};

  imreq.imr_interface.s_addr = INADDR_ANY;
  // IPV4 only
  imreq.imr_multiaddr.s_addr = mcast_addr->sin.sin_addr.s_addr;

  if ((ret = setsockopt(udp_socket->fd, IPPROTO_IP, IP_MULTICAST_IF, &iaddr, sizeof(struct in_addr))) < 0) {
    LOGE("Failed to set IP_MULTICAST_IF: %d", ret);
    return ret;
  }

  if ((ret = setsockopt(udp_socket->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq))) < 0) {
    LOGE("Failed to set IP_ADD_MEMBERSHIP: %d", ret);
    return ret;
  }

  return 0;
}

int udp_socket_open(UdpSocket* udp_socket, int family, int port) {
  udp_socket->receive_batch = NULL;
  udp_socket->receive_batch_primed = 0;
  int ret;
  int reuse = 1;
  int receive_buffer = 2 * 1024 * 1024;
  struct sockaddr* sa;
  socklen_t sock_len;

  udp_socket->bind_addr.family = family;
  switch (family) {
    case AF_INET6:
      udp_socket->fd = socket(AF_INET6, SOCK_DGRAM, 0);
      udp_socket->bind_addr.sin6.sin6_family = AF_INET6;
      udp_socket->bind_addr.sin6.sin6_port = htons(port);
      udp_socket->bind_addr.sin6.sin6_addr = in6addr_any;
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin6.sin6_port);
      sa = (struct sockaddr*)&udp_socket->bind_addr.sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      udp_socket->fd = socket(AF_INET, SOCK_DGRAM, 0);
      udp_socket->bind_addr.sin.sin_family = AF_INET;
      udp_socket->bind_addr.sin.sin_port = htons(port);
      udp_socket->bind_addr.sin.sin_addr.s_addr = htonl(INADDR_ANY);
      sa = (struct sockaddr*)&udp_socket->bind_addr.sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  if (udp_socket->fd < 0) {
    LOGE("Failed to create socket");
    return -1;
  }

  do {
    const int flags = fcntl(udp_socket->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(udp_socket->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      LOGE("Failed to make UDP socket nonblocking: %s", strerror(errno));
      ret = -1;
      break;
    }
    if (setsockopt(udp_socket->fd, SOL_SOCKET, SO_RCVBUF,
                   &receive_buffer, sizeof(receive_buffer)) < 0) {
      LOGW("Failed to enlarge UDP receive buffer: %s", strerror(errno));
    } else {
      socklen_t option_size = sizeof(receive_buffer);
      if (getsockopt(udp_socket->fd, SOL_SOCKET, SO_RCVBUF,
                     &receive_buffer, &option_size) == 0) {
        LOGI("UDP receive buffer: %d bytes", receive_buffer);
      }
    }

    if ((ret = setsockopt(udp_socket->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) < 0) {
      LOGW("reuse failed. ignore");
    }

    if ((ret = bind(udp_socket->fd, sa, sock_len)) < 0) {
      LOGE("Failed to bind socket: %d", ret);
      break;
    }

    if (getsockname(udp_socket->fd, sa, &sock_len) < 0) {
      LOGE("Get socket info failed");
      break;
    }
  } while (0);

  if (ret < 0) {
    udp_socket_close(udp_socket);
    return -1;
  }

  switch (udp_socket->bind_addr.family) {
    case AF_INET6:
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin6.sin6_port);
      break;
    case AF_INET:
    default:
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin.sin_port);
      break;
  }

  return 0;
}

void udp_socket_close(UdpSocket* udp_socket) {
  free(udp_socket->receive_batch);
  udp_socket->receive_batch = NULL;
  if (udp_socket->fd >= 0) {
    close(udp_socket->fd);
    udp_socket->fd = -1;
  }
}

int udp_socket_enable_receive_batch(UdpSocket* udp_socket) {
#if UDP_HAS_RECVMMSG
  if (!udp_socket->receive_batch && udp_socket->fd >= 0)
    udp_socket->receive_batch = calloc(1, sizeof(UdpReceiveBatch));
  return udp_socket->receive_batch != NULL;
#else
  (void)udp_socket;
  return 0;
#endif
}

#if UDP_HAS_RECVMMSG
static int udp_socket_receive_many(void* context, UdpReceiveRecord* records, unsigned count) {
  UdpSocket* socket = context;
  struct mmsghdr messages[UDP_RECEIVE_BATCH_CAPACITY] = {0};
  struct iovec vectors[UDP_RECEIVE_BATCH_CAPACITY];
  struct sockaddr_storage addresses[UDP_RECEIVE_BATCH_CAPACITY];
  for (unsigned i = 0; i < count; ++i) {
    vectors[i].iov_base = records[i].data;
    vectors[i].iov_len = sizeof(records[i].data);
    messages[i].msg_hdr.msg_iov = &vectors[i];
    messages[i].msg_hdr.msg_iovlen = 1;
    messages[i].msg_hdr.msg_name = &addresses[i];
    messages[i].msg_hdr.msg_namelen = sizeof(addresses[i]);
  }
  // No MSG_WAITFORONE and no timeout: MSG_DONTWAIT drains only available data.
  const int received = recvmmsg(socket->fd, messages, count, MSG_DONTWAIT, NULL);
  if (received < 0) {
    if (errno == ENOSYS || errno == EOPNOTSUPP || errno == EINVAL || errno == ENOMEM)
      return UDP_RECEIVE_BATCH_UNSUPPORTED;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
  }
  for (int i = 0; i < received; ++i) {
    records[i].size = (unsigned)messages[i].msg_len;
    records[i].address_size = messages[i].msg_hdr.msg_namelen;
    records[i].truncated = (messages[i].msg_hdr.msg_flags & MSG_TRUNC) != 0;
    if (records[i].address_size <= sizeof(records[i].address))
      memcpy(records[i].address, &addresses[i], records[i].address_size);
  }
  return received;
}
#endif

int udp_socket_sendto(UdpSocket* udp_socket, Address* addr, const uint8_t* buf, int len) {
  struct sockaddr* sa;
  socklen_t sock_len;
  int ret = -1;

  if (udp_socket->fd < 0) {
    LOGE("sendto before socket init");
    return -1;
  }

  switch (addr->family) {
    case AF_INET6:
      addr->sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr*)&addr->sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      addr->sin.sin_family = AF_INET;
      sa = (struct sockaddr*)&addr->sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  int send_flags = 0;
#ifdef MSG_DONTWAIT
  send_flags = MSG_DONTWAIT;
#endif
  if ((ret = sendto(udp_socket->fd, buf, len, send_flags, sa, sock_len)) < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return -1;
    LOGE("Failed to sendto: %s", strerror(errno));
    return -1;
  }

  return ret;
}

int udp_socket_recvfrom(UdpSocket* udp_socket, Address* addr, uint8_t* buf, int len) {
  struct sockaddr_in6 sin6;
  struct sockaddr_in sin;
  struct sockaddr* sa;
  socklen_t sock_len;
  int ret;

  if (udp_socket->fd < 0) {
    LOGE("recvfrom before socket init");
    return -1;
  }

  switch (udp_socket->bind_addr.family) {
    case AF_INET6:
      sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr*)&sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      sin.sin_family = AF_INET;
      sa = (struct sockaddr*)&sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  int recv_flags = 0;
#ifdef MSG_DONTWAIT
  recv_flags = MSG_DONTWAIT;
#endif
  ret = UDP_RECEIVE_BATCH_UNSUPPORTED;
#if UDP_HAS_RECVMMSG
  if (udp_socket->receive_batch && udp_socket->receive_batch_primed) {
    unsigned address_size = sock_len;
    ret = udp_receive_batch_read(udp_socket->receive_batch, buf, (unsigned)len,
                                sa, &address_size, udp_socket_receive_many, udp_socket);
    if (ret >= 0) sock_len = (socklen_t)address_size;
    if (ret == 0) { udp_socket->receive_batch_primed = 0; return 0; }
  }
#endif
  if (ret == UDP_RECEIVE_BATCH_UNSUPPORTED)
    ret = recvfrom(udp_socket->fd, buf, len, recv_flags, sa, &sock_len);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    LOGE("Failed to recvfrom: %s", strerror(errno));
    return -1;
  }

  udp_socket->receive_batch_primed = ret > 0;
  if (addr) {
    switch (udp_socket->bind_addr.family) {
      case AF_INET6:
        addr->family = AF_INET6;
        addr->port = htons(sin6.sin6_port);
        memcpy(&addr->sin6, &sin6, sizeof(struct sockaddr_in6));
        break;
      case AF_INET:
      default:
        addr->family = AF_INET;
        addr->port = htons(sin.sin_port);
        memcpy(&addr->sin, &sin, sizeof(struct sockaddr_in));
        break;
    }
  }

  return ret;
}

int tcp_socket_open(TcpSocket* tcp_socket, int family) {
  tcp_socket->bind_addr.family = family;
  switch (family) {
    case AF_INET6:
      tcp_socket->fd = socket(AF_INET6, SOCK_STREAM, 0);
      break;
    case AF_INET:
    default:
      tcp_socket->fd = socket(AF_INET, SOCK_STREAM, 0);
      break;
  }

  if (tcp_socket->fd < 0) {
    LOGE("Failed to create socket");
    return -1;
  }
  return 0;
}

int tcp_socket_connect(TcpSocket* tcp_socket, Address* addr) {
  char addr_string[ADDRSTRLEN];
  int ret;
  struct sockaddr* sa;
  socklen_t sock_len;

  if (tcp_socket->fd < 0) {
    LOGE("Connect before socket init");
    return -1;
  }

  switch (addr->family) {
    case AF_INET6:
      addr->sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr*)&addr->sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      addr->sin.sin_family = AF_INET;
      sa = (struct sockaddr*)&addr->sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  addr_to_string(addr, addr_string, sizeof(addr_string));
  LOGI("Connecting to server: %s:%d", addr_string, addr->port);
  if ((ret = connect(tcp_socket->fd, sa, sock_len)) < 0) {
    LOGE("Failed to connect to server");
    return -1;
  }

  LOGI("Server is connected");
  return 0;
}

void tcp_socket_close(TcpSocket* tcp_socket) {
  if (tcp_socket->fd > 0) {
    close(tcp_socket->fd);
  }
}

int tcp_socket_send(TcpSocket* tcp_socket, const uint8_t* buf, int len) {
  int ret;

  if (tcp_socket->fd < 0) {
    LOGE("sendto before socket init");
    return -1;
  }

  ret = send(tcp_socket->fd, buf, len, 0);
  if (ret < 0) {
    LOGE("Failed to send: %s", strerror(errno));
    return -1;
  }
  return ret;
}

int tcp_socket_recv(TcpSocket* tcp_socket, uint8_t* buf, int len) {
  int ret;

  if (tcp_socket->fd < 0) {
    LOGE("recvfrom before socket init");
    return -1;
  }

  ret = recv(tcp_socket->fd, buf, len, 0);
  if (ret < 0) {
    LOGE("Failed to recv: %s", strerror(errno));
    return -1;
  }
  return ret;
}
