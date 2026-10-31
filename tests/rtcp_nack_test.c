#include "rtcp.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    uint8_t packet[16];
    assert(rtcp_get_nack(packet, sizeof(packet), 0x01020304u, 0x11223344u,
                         0x5566u, 0xa55au) == 16);
    assert(packet[0] == 0x81);
    assert(packet[1] == RTCP_RTPFB);
    assert(packet[2] == 0 && packet[3] == 3);
    assert(packet[4] == 1 && packet[5] == 2 && packet[6] == 3 && packet[7] == 4);
    assert(packet[8] == 0x11 && packet[9] == 0x22 && packet[10] == 0x33 && packet[11] == 0x44);
    assert(packet[12] == 0x55 && packet[13] == 0x66);
    assert(packet[14] == 0xa5 && packet[15] == 0x5a);

    return 0;
}
