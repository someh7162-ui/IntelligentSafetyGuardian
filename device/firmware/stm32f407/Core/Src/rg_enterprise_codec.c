#include "rg_enterprise_codec.h"
#include "rg_json.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
static rg_json_token_t tokens[384];
void rg_event_id(char*out,size_t cap,uint64_t boot,uint32_t seq){snprintf(out,cap,"%08lx%08lx-%08lx",(unsigned long)(boot>>32),(unsigned long)(boot&0xffffffffU),(unsigned long)seq);}
static bool add(char*out,size_t cap,size_t*used,const char*fmt,...){if(*used>=cap)return false;va_list ap;va_start(ap,fmt);int n=vsnprintf(out+*used,cap-*used,fmt,ap);va_end(ap);if(n<0||(size_t)n>=cap-*used)return false;*used+=(size_t)n;return true;}
int rg_event_json(char*out,size_t cap,const char*device,const rg_event_t*e){
    char id[40],group[40],stamp[32],lat[32]="null",lng[32]="null",before[24]="null",after[24]="null";
    rg_event_id(id,sizeof id,e->boot,e->seq);rg_event_id(group,sizeof group,e->group_seq?e->group_boot:e->boot,e->group_seq?e->group_seq:e->seq);
    if(e->captured_at_ms)snprintf(stamp,sizeof stamp,"%lld",(long long)e->captured_at_ms);else strcpy(stamp,"null");
    if(e->gps_valid){snprintf(lat,sizeof lat,"%.7f",e->lat_e7*1e-7);snprintf(lng,sizeof lng,"%.7f",e->lng_e7*1e-7);}
    if(e->before_valid)snprintf(before,sizeof before,"%.3f",e->before_milli_kph*.001);
    if(e->after_valid)snprintf(after,sizeof after,"%.3f",e->after_milli_kph*.001);
    const char*type=e->kind==RG_BRAKE?"HARSH_BRAKE":e->kind==RG_OVERSPEED?"OVERSPEED":"INTERSECTION_REVIEW";
    size_t n=0;
    if(!add(out,cap,&n,"{\"deviceId\":\"%s\",\"batchId\":\"%s\",\"events\":[{\"eventId\":\"%s\",\"type\":\"%s\",\"policyVersion\":%lu,\"capturedAtMs\":%s,\"startUptimeMs\":%lu,\"qualifiedUptimeMs\":%lu,\"endUptimeMs\":%lu,\"gpsValid\":%s,\"latitude\":%s,\"longitude\":%s,\"gpsAgeMs\":%u,\"speedBeforeKph\":%s,\"speedAfterKph\":%s,\"peakSpeedKph\":%.3f,\"limitKph\":%.3f,\"minForwardMs2\":%.3f,\"intersectionId\":%lu,\"partial\":%s,\"mock\":false,\"trace\":[",
        device,group,id,type,(unsigned long)e->policy_version,stamp,(unsigned long)e->start_ms,(unsigned long)e->qualified_ms,(unsigned long)e->end_ms,e->gps_valid?"true":"false",lat,lng,e->gps_age_ms,before,after,e->peak_milli_kph*.001,e->limit_milli_kph*.001,e->min_forward_milli_ms2*.001,(unsigned long)e->intersection_id,e->partial?"true":"false"))return -1;
    for(unsigned i=0;i<e->trace_count;i++)if(!add(out,cap,&n,"%s{\"offsetMs\":%ld,\"forwardMs2\":%.3f,\"verticalMs2\":%.3f,\"gyroDps\":%.1f}",i?",":"",(long)(int32_t)(e->trace[i].at-e->start_ms),e->trace[i].forward_milli_ms2*.001,e->trace[i].vertical_milli_ms2*.001,e->trace[i].gyro_deci_dps*.1))return -1;
    if(!add(out,cap,&n,"]}]}"))return -1;
    return (int)n;
}
static bool number(const rg_json_t*j,int p,const char*key,double*out){return rg_json_number(j,rg_json_get(j,p,key),out);}
static bool uint_field(const rg_json_t*j,int p,const char*key,uint32_t*out){double n;if(!number(j,p,key,&n)||n<0||n>UINT32_MAX||floor(n)!=n)return false;*out=(uint32_t)n;return true;}
bool rg_policy_json(const char*text,size_t length,rg_policy_t*out,rg_road_t*roads,uint8_t*count){
    rg_json_t j;if(!rg_json_parse(&j,text,length,tokens,384))return false;
    int p=rg_json_get(&j,0,"policy"),array=rg_json_get(&j,0,"intersections");if(p<0||array<0||j.tokens[array].type!='[')return false;
    rg_policy_t policy=rg_supervision_defaults();double n;
    if(!uint_field(&j,p,"version",&policy.version)||!policy.version||!uint_field(&j,p,"speedingHoldMs",&policy.speeding_hold_ms)
       ||!uint_field(&j,p,"recoveryMs",&policy.recovery_ms)||!uint_field(&j,p,"windowMs",&policy.window_ms)
       ||!uint_field(&j,p,"countThreshold",&policy.threshold)||!uint_field(&j,p,"brakeHoldMs",&policy.brake_hold_ms)
       ||!number(&j,p,"brakeThresholdMs2",&n))return false;
    policy.brake_milli_ms2=(uint32_t)(n*1000.0);
    if(!number(&j,p,"forwardAxis",&n)||n < -3||n>3||n==0||floor(n)!=n)return false;
    policy.forward_axis=(int8_t)n;
    int enabled=rg_json_get(&j,p,"enabled");if(rg_json_equals(&j,enabled,"true"))policy.enabled=true;else if(rg_json_equals(&j,enabled,"false"))policy.enabled=false;else return false;
    int limit=rg_json_get(&j,p,"speedLimitKph");
    if(!rg_json_equals(&j,limit,"null")){if(!rg_json_number(&j,limit,&n)||n<=0||n>80)return false;policy.limit_milli_kph=(uint32_t)(n*1000);}
    unsigned used=0;
    for(int i=array+1;i<j.count&&j.tokens[i].start<j.tokens[array].end;i++)if(j.tokens[i].parent==array){
        if(used>=RG_ROADS||j.tokens[i].type!='{')return false;
        rg_road_t*r=&roads[used++];memset(r,0,sizeof *r);
        if(!uint_field(&j,i,"id",&r->id)||!r->id||!uint_field(&j,i,"radiusM",&r->radius_m)||!number(&j,i,"latitude",&n)||n < -90||n>90)return false;
        r->lat_e7=(int32_t)(n*1e7);
        if(!number(&j,i,"longitude",&n)||n < -180||n>180)return false;
        r->lng_e7=(int32_t)(n*1e7);
        int on=rg_json_get(&j,i,"enabled");if(rg_json_equals(&j,on,"true"))r->enabled=true;else if(!rg_json_equals(&j,on,"false"))return false;
    }
    *out=policy;*count=(uint8_t)used;return true;
}
bool rg_ack_json(const char*text,size_t length,const char*id,bool image){
    rg_json_t j;if(!rg_json_parse(&j,text,length,tokens,384)||!rg_json_equals(&j,rg_json_get(&j,0,"status"),"SAVED"))return false;
    if(image)return rg_json_equals(&j,rg_json_get(&j,0,"photoId"),id);
    int a=rg_json_get(&j,0,"acknowledgedEventIds");if(a<0||j.tokens[a].type!='[')return false;
    for(int i=a+1;i<j.count&&j.tokens[i].start<j.tokens[a].end;i++)if(j.tokens[i].parent==a&&j.tokens[i].type=='"'&&rg_json_equals(&j,i,id))return true;
    return false;
}
