// Exercise the actual production socket.c adapter with deterministic OS calls.
// This test does not claim to reproduce Nintendo's socket-service scheduling.
#define __linux__ 1
#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#ifndef F_GETFL
#define F_GETFL 0
#define F_SETFL 1
#define O_NONBLOCK 0x800
#endif
int fcntl(int, int, ...);
#include <winsock2.h>
int WSAAPI fake_recvfrom(SOCKET,char*,int,int,struct sockaddr*,int*);
#define recvfrom fake_recvfrom
#include "socket.c"
#undef recvfrom
#include <assert.h>

static unsigned pending, value, single_calls, many_calls;
static int unsupported, truncate_next;
int fcntl(int fd,int command,...) { (void)fd;(void)command;assert(0);return -1; }
int addr_to_string(const Address* address,char* buf,size_t size) {
    (void)address;(void)buf;(void)size;assert(0);return -1;
}
int recvmmsg(int fd, struct mmsghdr* messages, unsigned count, int flags, struct timespec* timeout) {
    (void)fd; assert(count==8 && flags==MSG_DONTWAIT && timeout==NULL);
    ++many_calls;
    if(unsupported) {errno=ENOSYS;return -1;}
    unsigned returned=pending<count?pending:count;
    if(!returned) {errno=EAGAIN;return -1;}
    for(unsigned i=0;i<returned;++i) {
        uint8_t* buf=messages[i].msg_hdr.msg_iov[0].iov_base;
        buf[0]=(uint8_t)value++; buf[1]=99;
        struct sockaddr_in source={0}; source.sin_family=AF_INET;source.sin_port=htons(1234);
        memcpy(messages[i].msg_hdr.msg_name,&source,sizeof(source));
        messages[i].msg_hdr.msg_namelen=sizeof(source);
        messages[i].msg_hdr.msg_flags=truncate_next?MSG_TRUNC:0;
        truncate_next=0; messages[i].msg_len=2;
    }
    pending-=returned;return (int)returned;
}
int WSAAPI fake_recvfrom(SOCKET fd,char* buf,int size,int flags,struct sockaddr* address,int* length) {
    (void)fd;(void)size;assert(flags==MSG_DONTWAIT);++single_calls;
    if(!pending) {errno=EAGAIN;return -1;}
    --pending;buf[0]=(char)value++;buf[1]=99;
    struct sockaddr_in source={0};source.sin_family=AF_INET;source.sin_port=htons(1234);
    memcpy(address,&source,sizeof(source));*length=sizeof(source);return 2;
}
int main(void) {
    UdpSocket socket={.fd=1}; socket.bind_addr.family=AF_INET;
    assert(udp_socket_enable_receive_batch(&socket));
    uint8_t buf[16]; Address source={0}; pending=17;
    for(unsigned i=0;i<17;++i) {
        assert(udp_socket_recvfrom(&socket,&source,buf,sizeof(buf))==2);
        assert(buf[0]==i && buf[1]==99 && source.family==AF_INET && source.port==1234);
    }
    assert(single_calls==1 && many_calls==2);
    assert(udp_socket_recvfrom(&socket,NULL,buf,sizeof(buf))==0);
    const unsigned calls=many_calls;
    for(unsigned i=0;i<20;++i) assert(udp_socket_recvfrom(&socket,NULL,buf,sizeof(buf))==0);
    assert(many_calls==calls); // empty polls never allocate a multi-message call
    pending=3;assert(udp_socket_recvfrom(&socket,NULL,buf,sizeof(buf))==2);
    unsupported=1;
    assert(udp_socket_recvfrom(&socket,NULL,buf,sizeof(buf))==2);
    assert(udp_socket_recvfrom(&socket,NULL,buf,sizeof(buf))==2);
    assert(many_calls==calls+1 && socket.receive_batch->unsupported);
    socket.receive_batch->unsupported=0;unsupported=0;pending=3;
    truncate_next=1; // corrupted record is discarded, next complete one survives
    assert(udp_socket_recvfrom(&socket,NULL,buf,sizeof(buf))==2);
    assert(socket.receive_batch->rejected==1);
    free(socket.receive_batch);
    puts("Production UDP adapter batching/idle/fallback/truncation tests passed.");
}
