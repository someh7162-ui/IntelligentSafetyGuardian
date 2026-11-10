#ifndef RG_ENTERPRISE_CODEC_H
#define RG_ENTERPRISE_CODEC_H
#include "rg_supervision.h"
#include <stddef.h>
int rg_event_json(char *,size_t,const char *,const rg_event_t *);
bool rg_policy_json(const char *,size_t,rg_policy_t *,rg_road_t *,uint8_t *);
bool rg_ack_json(const char *,size_t,const char *,bool);
void rg_event_id(char *,size_t,uint64_t,uint32_t);
#endif
