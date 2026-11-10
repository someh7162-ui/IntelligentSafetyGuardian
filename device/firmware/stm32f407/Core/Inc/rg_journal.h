#ifndef RG_JOURNAL_H
#define RG_JOURNAL_H
#include <stdint.h>
#include <stdbool.h>
#define RG_JOURNAL_SECTOR 4096U
#define RG_JOURNAL_MAX 3584U
#define RG_JOURNAL_SLOTS 256U
#define RG_JOURNAL_EVENT 1U
#define RG_JOURNAL_PHOTO 2U
#define RG_JOURNAL_CONFIG 3U
#define RG_JOURNAL_PENDING 0x7fU
#define RG_JOURNAL_READY 0x3fU
#define RG_JOURNAL_ACK 0x1fU
#pragma pack(push,1)
typedef struct {
    uint32_t magic,header_crc,payload_crc; uint64_t boot;
    uint32_t seq; uint16_t length,part; uint8_t type,status; uint16_t reserved;
} rg_journal_header_t;
#pragma pack(pop)
typedef struct {
    void *context;
    bool (*read)(void *,uint32_t,void *,uint32_t);
    bool (*program)(void *,uint32_t,const void *,uint32_t);
    bool (*erase)(void *,uint32_t);
    int (*busy)(void *); /* 0 ready, 1 busy, -1 fault; operations never wait. */
    bool (*reclaim)(void *,const rg_journal_header_t *,unsigned);
} rg_journal_io_t;
typedef struct {
    rg_journal_io_t io;uint32_t base;uint16_t slots,cursor,slot,offset;
    bool writable,fault;uint8_t state;int result;uint32_t full_count;
    rg_journal_header_t header;uint8_t data[RG_JOURNAL_MAX];
    rg_journal_header_t index[RG_JOURNAL_SLOTS];uint8_t occupied[RG_JOURNAL_SLOTS];
} rg_journal_t;
uint32_t rg_crc32(const void *,uint32_t);
bool rg_journal_init(rg_journal_t *,rg_journal_io_t,uint32_t,uint16_t,bool);
bool rg_journal_append(rg_journal_t *,uint8_t,uint64_t,uint32_t,uint16_t,const void *,uint16_t,bool);
bool rg_journal_mark(rg_journal_t *,unsigned,uint8_t);
void rg_journal_task(rg_journal_t *);
int rg_journal_find(const rg_journal_t *,uint8_t,uint64_t,uint32_t,uint16_t);
bool rg_journal_read(rg_journal_t *,unsigned,void *,uint16_t);
#endif
