#include "rg_journal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t flash[4096*8];static unsigned operations;
static bool read_flash(void*p,uint32_t a,void*out,uint32_t n){(void)p;if(a+n>sizeof flash)return false;memcpy(out,flash+a,n);return true;}
static bool program(void*p,uint32_t a,const void*data,uint32_t n){(void)p;if(a+n>sizeof flash||((a&255)+n)>256)return false;const uint8_t*d=data;for(unsigned i=0;i<n;i++){if((flash[a+i]&d[i])!=d[i])return false;flash[a+i]&=d[i];}operations++;return true;}
static bool erase(void*p,uint32_t a){(void)p;assert(a%4096==0);memset(flash+a,255,4096);operations++;return true;}
static int busy(void*p){(void)p;return 0;}
static rg_journal_io_t io={NULL,read_flash,program,erase,busy,NULL};
static rg_journal_t j,reboot;
static void finish(void){for(unsigned i=0;j.state&&i<100;i++)rg_journal_task(&j);assert(!j.state&&j.result==1);}
int main(void){
 uint8_t payload[1800];memset(payload,42,sizeof payload);
 memset(flash,255,sizeof flash);assert(rg_journal_init(&j,io,0,8,false));assert(!rg_journal_append(&j,1,2,3,0,payload,sizeof payload,true));assert(operations==0);
 flash[500]=1;assert(rg_journal_init(&j,io,0,8,true));assert(!j.writable&&operations==0); /* unknown user data */
 memset(flash,255,sizeof flash);assert(rg_journal_init(&j,io,0,8,true));assert(rg_journal_append(&j,1,2,3,0,payload,sizeof payload,false));finish();int id=rg_journal_find(&j,1,2,3,0);assert(id>=0&&j.index[id].status==RG_JOURNAL_PENDING);
 assert(rg_journal_mark(&j,id,RG_JOURNAL_READY));finish();assert(rg_journal_init(&reboot,io,0,8,true));assert(reboot.index[id].status==RG_JOURNAL_READY);
 assert(rg_journal_mark(&j,id,RG_JOURNAL_ACK));finish();assert(!rg_journal_mark(&j,id,RG_JOURNAL_READY));
 for(unsigned cut=0;cut<16;cut++){
  memset(flash,255,sizeof flash);rg_journal_init(&j,io,0,8,true);rg_journal_append(&j,1,9,cut,0,payload,sizeof payload,true);
  for(unsigned step=0;step<cut;step++)rg_journal_task(&j);
  assert(rg_journal_init(&reboot,io,0,8,true));int slot=rg_journal_find(&reboot,1,9,cut,0);
  if(slot>=0){uint8_t out[1800];assert(rg_journal_read(&reboot,slot,out,sizeof out));assert(!memcmp(payload,out,sizeof payload));}
 }
 puts("Journal power-cut, read-only, foreign-data and monotonic ACK scenarios passed");return 0;
}
