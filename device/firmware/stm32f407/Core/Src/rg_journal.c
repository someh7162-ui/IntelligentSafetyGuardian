#include "rg_journal.h"
#include <string.h>
#include <stddef.h>
#define MAGIC 0x314a4752U
_Static_assert(sizeof(rg_journal_header_t)==32,"journal wire header");
uint32_t rg_crc32(const void *data,uint32_t n){const uint8_t *p=data;uint32_t c=~0U;while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);}return ~c;}
static uint32_t header_crc(rg_journal_header_t h){h.header_crc=0;h.status=0xff;return rg_crc32(&h,sizeof h);}
static bool valid_header(const rg_journal_header_t *h){return h->magic==MAGIC&&h->header_crc==header_crc(*h)&&h->length<=RG_JOURNAL_MAX&&h->type>=1&&h->type<=3;}
static uint32_t address(rg_journal_t*j,unsigned slot){return j->base+slot*RG_JOURNAL_SECTOR;}
bool rg_journal_init(rg_journal_t *j,rg_journal_io_t io,uint32_t base,uint16_t slots,bool writes){
    if(!j||!io.read||!io.program||!io.erase||!io.busy||slots==0||slots>RG_JOURNAL_SLOTS||base%RG_JOURNAL_SECTOR)return false;
    memset(j,0,sizeof(*j));j->io=io;j->base=base;j->slots=slots;j->writable=writes;
    uint8_t block[256];
    for(unsigned s=0;s<slots;s++){
        rg_journal_header_t h;if(!io.read(io.context,address(j,s),&h,sizeof h)){j->fault=true;j->writable=false;return false;}
        if(valid_header(&h)){
            j->occupied[s]=2; /* Owned but uncommitted records are never uploaded. */
            if(h.status==RG_JOURNAL_PENDING||h.status==RG_JOURNAL_READY||h.status==RG_JOURNAL_ACK){
                if(!io.read(io.context,address(j,s)+sizeof h,j->data,h.length)){j->fault=true;j->writable=false;return false;}
                if(rg_crc32(j->data,h.length)==h.payload_crc){j->occupied[s]=1;j->index[s]=h;}
            }
        }else{
            bool blank=true;
            for(unsigned off=0;off<RG_JOURNAL_SECTOR;off+=sizeof block){
                if(!io.read(io.context,address(j,s)+off,block,sizeof block)){j->fault=true;return false;}
                for(unsigned k=0;k<sizeof block;k++)if(block[k]!=0xff)blank=false;
            }
            if(!blank){j->occupied[s]=3;j->writable=false;} /* Foreign or ambiguous data is never erased. */
        }
    }
    return true;
}
int rg_journal_find(const rg_journal_t*j,uint8_t type,uint64_t boot,uint32_t seq,uint16_t part){
    for(unsigned s=0;s<j->slots;s++)if(j->occupied[s]==1&&j->index[s].type==type&&j->index[s].boot==boot&&j->index[s].seq==seq&&j->index[s].part==part)return (int)s;
    return -1;
}
bool rg_journal_read(rg_journal_t*j,unsigned slot,void*out,uint16_t max){
    if(slot>=j->slots||j->occupied[slot]!=1||j->index[slot].length>max)return false;
    return j->io.read(j->io.context,address(j,slot)+sizeof(rg_journal_header_t),out,j->index[slot].length);
}
bool rg_journal_append(rg_journal_t*j,uint8_t type,uint64_t boot,uint32_t seq,uint16_t part,const void*data,uint16_t length,bool ready){
    if(!j->writable||j->fault||j->state||length>RG_JOURNAL_MAX||!length||type<1||type>3||!data)return false;
    if(rg_journal_find(j,type,boot,seq,part)>=0){j->result=1;return true;}
    int chosen=-1;bool erase=false;
    for(unsigned i=0;i<j->slots;i++){
        unsigned s=(j->cursor+i)%j->slots;
        if(j->occupied[s]==0){chosen=(int)s;break;}
        if(j->occupied[s]==1&&j->index[s].status==RG_JOURNAL_ACK&&
           (!j->io.reclaim||j->io.reclaim(j->io.context,&j->index[s],s))){chosen=(int)s;erase=true;break;}
        /* Incomplete owned records stay reserved for diagnosis; never erase ambiguous headers. */
    }
    if(chosen<0){j->full_count++;return false;}
    j->slot=(uint16_t)chosen;j->offset=0;j->result=0;memcpy(j->data,data,length);
    rg_journal_header_t h={MAGIC,0,rg_crc32(data,length),boot,seq,length,part,type,0xff,0};h.header_crc=header_crc(h);
    j->header=h;j->header.status=ready?RG_JOURNAL_READY:RG_JOURNAL_PENDING;
    if(erase){if(!j->io.erase(j->io.context,address(j,j->slot)))return false;j->state=1;}
    else j->state=2;
    return true;
}
bool rg_journal_mark(rg_journal_t*j,unsigned slot,uint8_t status){
    if(!j->writable||j->fault||j->state||slot>=j->slots||j->occupied[slot]!=1)return false;
    uint8_t old=j->index[slot].status;
    if(status!=RG_JOURNAL_READY&&status!=RG_JOURNAL_ACK)return false;
    if((old&status)!=status)return false;
    if(old==status){j->result=1;return true;}
    j->slot=(uint16_t)slot;j->header=j->index[slot];j->header.status=status;j->result=0;
    if(!j->io.program(j->io.context,address(j,slot)+offsetof(rg_journal_header_t,status),&j->header.status,1))return false;
    j->state=6;return true;
}
void rg_journal_task(rg_journal_t*j){
    if(!j->state||j->fault)return;
    int busy=j->io.busy(j->io.context);if(busy>0)return;
    if(busy<0){j->fault=true;j->result=-1;j->state=0;return;}
    bool ok=true;
    switch(j->state){
    case 1:j->occupied[j->slot]=0;j->state=2;break;
    case 2:{rg_journal_header_t h=j->header;h.status=0xff;ok=j->io.program(j->io.context,address(j,j->slot),&h,sizeof h);j->state=3;break;}
    case 3:if(j->offset<j->header.length){
        uint32_t addr=address(j,j->slot)+sizeof(j->header)+j->offset;
        unsigned remaining=(unsigned)j->header.length-j->offset;
        unsigned n=256U-(addr&255U);if(n>remaining)n=remaining;
        ok=j->io.program(j->io.context,addr,j->data+j->offset,n);j->offset=(uint16_t)(j->offset+n);
    }else j->state=4;break;
    case 4:ok=j->io.program(j->io.context,address(j,j->slot)+offsetof(rg_journal_header_t,status),&j->header.status,1);j->state=5;break;
    case 5:case 6:j->index[j->slot]=j->header;j->occupied[j->slot]=1;j->cursor=(uint16_t)((j->slot+1U)%j->slots);j->state=0;j->result=1;break;
    default:ok=false;break;
    }
    if(!ok){j->fault=true;j->state=0;j->result=-1;}
}
