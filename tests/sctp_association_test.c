// Real production SCTP implementation; only DTLS encryption and entropy are
// replaced. Wire packets below use an independent bitwise CRC32c reference.
#define CONFIG_USE_USRSCTP 0
#define DTLS_SRTP_H_
#include <stddef.h>
#include <stdint.h>
#include "address.h"
typedef struct DtlsSrtp { int ctr_drbg; } DtlsSrtp;
int dtls_srtp_write(DtlsSrtp*,const unsigned char*,size_t);
int mbedtls_ctr_drbg_random(void*,unsigned char*,size_t);
#include "sctp.c"
#include <assert.h>

_Static_assert(sizeof(SctpHeader)==12,"SCTP common wire header");
static uint8_t wire[32][CONFIG_MTU];
static size_t sizes[32];
static unsigned writes, opens, messages, entropy=7;
static int rng_failed;
int dtls_srtp_write(DtlsSrtp* context,const unsigned char* data,size_t size) {
    (void)context;assert(writes<32 && size<=CONFIG_MTU);
    memcpy(wire[writes],data,size);sizes[writes++]=size;return (int)size;
}
int mbedtls_ctr_drbg_random(void* context,unsigned char* data,size_t size) {
    (void)context;if(rng_failed)return -1;
    for(size_t i=0;i<size;++i)data[i]=(uint8_t)++entropy;
    return 0;
}
uint32_t ports_get_epoch_time(void) { return 10000; }
static void opened(void* context) {(void)context;++opens;}
static void message(char* data,size_t size,void* context,uint16_t sid) {
    (void)context;assert(size==1 && data[0]==42 && sid==0);++messages;
}
static void put16(uint8_t* p,uint16_t x) {p[0]=(uint8_t)(x>>8);p[1]=(uint8_t)x;}
static void put32(uint8_t* p,uint32_t x) {p[0]=(uint8_t)(x>>24);p[1]=(uint8_t)(x>>16);p[2]=(uint8_t)(x>>8);p[3]=(uint8_t)x;}
static uint32_t get32(const uint8_t* p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint16_t get16(const uint8_t* p) {return (uint16_t)((p[0]<<8)|p[1]);}
static void checksum(uint8_t* p,size_t size) {
    memset(p+8,0,4);uint32_t crc=~0u;
    for(size_t i=0;i<size;++i) {
        crc^=p[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0x82f63b78u:0);
    }
    crc=~crc; // SCTP CRC is serialized least significant byte first.
    for(unsigned i=0;i<4;++i)p[8+i]=(uint8_t)(crc>>(i*8));
}
static size_t packet(uint8_t* p,uint8_t type,uint16_t chunk_size,uint32_t tag) {
    const size_t size=12+((chunk_size+3u)&~3u);
    memset(p,0,size);put16(p,5000);put16(p+2,5000);put32(p+4,tag);
    p[12]=type;put16(p+14,chunk_size);return size;
}
static void init(Sctp* s,DtlsSrtp* dtls) {
    memset(s,0,sizeof(*s));writes=opens=messages=0;
    s->onopen=opened;s->onmessage=message;
    assert(sctp_create_association(s,dtls)==0 && writes==1 && sizes[0]==32);
    assert(get32(wire[0]+16)==s->local_verification_tag && s->local_verification_tag!=0x12345678);
    assert(get16(wire[0]+24)==SCTP_MAX_STREAMS && get16(wire[0]+26)==SCTP_MAX_STREAMS);
}
int main(void) {
    Sctp s;DtlsSrtp dtls={0};uint8_t p[CONFIG_MTU];
    init(&s,&dtls);const uint32_t tag=s.local_verification_tag;
    // Server-initiated/simultaneous INIT with a tighter inbound limit, as
    // allowed by WebRTC SCTP. The former 65535-stream INIT_ACK violates it.
    size_t size=packet(p,SCTP_INIT,32,0);put32(p+16,0x98761234);put32(p+20,0x100000);
    put16(p+24,1024);put16(p+26,2);put32(p+28,9000);
    put16(p+32,0x8008);put16(p+34,8);checksum(p,size);
    sctp_incoming_data(&s,(char*)p,size);
    assert(writes==2 && wire[1][12]==SCTP_INIT_ACK && sizes[1]==52);
    assert(get16(wire[1]+24)==2 && get16(wire[1]+26)==SCTP_MAX_STREAMS);
    assert(get32(wire[1]+16)==tag && get32(wire[1]+4)==0x98761234 && get32(wire[1]+28)==1234);
    uint8_t reply[CONFIG_MTU];memcpy(reply,wire[1],sizes[1]);
    const uint8_t cookie[16]={0}; // a forged cookie must not open the channel
    size=packet(p,SCTP_COOKIE_ECHO,20,tag);memcpy(p+16,cookie,16);checksum(p,size);
    sctp_incoming_data(&s,(char*)p,size);assert(!s.connected && opens==0 && writes==2);
    // Valid COOKIE_ECHO can be bundled with data. Both chunks must survive.
    size=packet(p,SCTP_COOKIE_ECHO,20,tag);memcpy(p+16,reply+36,16);
    p[32]=SCTP_DATA;put16(p+34,17);put32(p+36,9000);put16(p+40,0);put16(p+42,0);put32(p+44,51);p[48]=42;
    size=52;memset(p+49,0,3);checksum(p,size);
    sctp_incoming_data(&s,(char*)p,size);
    assert(s.connected && opens==1 && messages==1 && s.handshake_retry.size==0);
    assert(wire[2][12]==SCTP_COOKIE_ACK && wire[3][12]==SCTP_SACK);
    // Preserve the existing client-initiated path and variable-length cookies.
    init(&s,&dtls);
    size=packet(p,SCTP_INIT_ACK,32,s.local_verification_tag);
    put32(p+16,0x33445566);put32(p+20,0x100000);put16(p+24,2);put16(p+26,5);put32(p+28,8000);
    put16(p+32,7);put16(p+34,12);memcpy(p+36,"abcdefgh",8);checksum(p,size);
    sctp_incoming_data(&s,(char*)p,size);
    assert(writes==2 && wire[1][12]==SCTP_COOKIE_ECHO && sizes[1]==24);
    assert(!memcmp(wire[1]+16,"abcdefgh",8) && get32(wire[1]+4)==0x33445566);
    sctp_retransmit_pending(&s); // retry deadline has not elapsed
    assert(writes==2 && s.handshake_retry.bytes[12]==SCTP_COOKIE_ECHO);
    size=packet(p,SCTP_COOKIE_ACK,4,s.local_verification_tag^1);checksum(p,size);
    sctp_incoming_data(&s,(char*)p,size);assert(!s.connected);
    size=packet(p,SCTP_COOKIE_ACK,4,s.local_verification_tag);checksum(p,size);
    sctp_incoming_data(&s,(char*)p,size);assert(s.connected && opens==1 && s.handshake_retry.size==0);
    // Malformed lengths, missing cookie, zero counts and bad CRC do not write
    // outside the packet or mark a failed association as ready.
    init(&s,&dtls);
    for(unsigned bad=0;bad<6;++bad) {
        size=packet(p,SCTP_INIT_ACK,28,s.local_verification_tag);
        put32(p+16,1);put32(p+20,0x100000);put16(p+24,2);put16(p+26,5);
        put16(p+32,7);put16(p+34,8);put32(p+36,55);
        if(bad==0)put16(p+14,0);
        if(bad==1)put16(p+14,1000);
        if(bad==2)put16(p+34,1000);
        if(bad==3)put16(p+24,0);
        if(bad==4)put16(p+24,6);
        checksum(p,size);if(bad==5)p[8]^=1;
        sctp_incoming_data(&s,(char*)p,size);assert(writes==1 && !s.connected);
    }
    for(size_t truncated=0;truncated<16;++truncated)sctp_incoming_data(&s,(char*)p,truncated);
    rng_failed=1;assert(sctp_create_association(&s,&dtls)<0);
    puts("Production SCTP active/passive/collision/stream-limits/cookie/bounds tests passed.");
}
