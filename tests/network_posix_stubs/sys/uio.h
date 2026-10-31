#pragma once
#include <stddef.h>
#include <time.h>
struct iovec { void* iov_base; size_t iov_len; };
struct msghdr {
    void* msg_name; int msg_namelen;
    struct iovec* msg_iov; size_t msg_iovlen;
    int msg_flags;
};
struct mmsghdr { struct msghdr msg_hdr; unsigned msg_len; };
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
#ifndef MSG_TRUNC
#define MSG_TRUNC 0x20
#endif

int recvmmsg(int, struct mmsghdr*, unsigned, int, struct timespec*);
