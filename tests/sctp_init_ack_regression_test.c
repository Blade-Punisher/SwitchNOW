#define CONFIG_USE_USRSCTP 0
#define DTLS_SRTP_H_
#include <stddef.h>
#include <stdint.h>
#include "address.h"
typedef struct DtlsSrtp {int ctr_drbg;} DtlsSrtp;
int dtls_srtp_write(DtlsSrtp*,const unsigned char*,size_t);
int mbedtls_ctr_drbg_random(void*,unsigned char*,size_t);
#include "sctp.c"
#include <assert.h>
static uint8_t reply[CONFIG_MTU];
int dtls_srtp_write(DtlsSrtp* context,const unsigned char* data,size_t size) {
    (void)context;memcpy(reply,data,size);return (int)size;
}
int mbedtls_ctr_drbg_random(void* context,unsigned char* data,size_t size) {
    (void)context;memset(data,42,size);return 0;
}
uint32_t ports_get_epoch_time(void) {return 10000;}
int main(void) {
    Sctp s={0};DtlsSrtp dtls={0};assert(sctp_create_association(&s,&dtls)==0);
    uint8_t packet[32]={0x13,0x88,0x13,0x88};
    packet[12]=1;packet[15]=20;packet[19]=1;packet[21]=0x10;
    packet[25]=2;packet[27]=2;packet[31]=1;
    uint32_t crc=~0u;
    for(size_t i=0;i<sizeof(packet);++i) {
        crc^=packet[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0x82f63b78u:0);
    }
    crc=~crc;for(unsigned i=0;i<4;++i)packet[8+i]=(uint8_t)(crc>>(8*i));
    sctp_incoming_data(&s,(char*)packet,sizeof(packet));
    const unsigned offered=(reply[24]<<8)|reply[25];
    printf("SCTP INIT_ACK offered OS=%u; peer MIS=2\n",offered);
    return offered<=2 ? 0 : 42;
}
