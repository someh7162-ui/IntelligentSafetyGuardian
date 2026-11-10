#ifndef DFPLAYER_FRAME_H
#define DFPLAYER_FRAME_H
#include <stdint.h>
static inline void dfplayer_query(uint8_t command, uint8_t frame[10]) {
    const uint8_t base[10] = {0x7e,0xff,0x06,0,0,0,0,0,0,0xef};
    for (unsigned i=0;i<10;i++) frame[i]=base[i];
    frame[3]=command;
    uint16_t checksum=(uint16_t)(0u-(0xffu+6u+command));
    frame[7]=(uint8_t)(checksum>>8); frame[8]=(uint8_t)checksum;
}
static inline int dfplayer_valid(const uint8_t frame[10]) {
    if(frame[0]!=0x7e || frame[1]!=0xff || frame[2]!=6 || frame[9]!=0xef) return 0;
    uint16_t sum=0;
    for(unsigned i=1;i<=6;i++) sum=(uint16_t)(sum+frame[i]);
    return (uint16_t)(0u-sum)==(uint16_t)(((uint16_t)frame[7]<<8)|frame[8]);
}
#endif
