#include "rg_supervision.h"
#include <math.h>
#include <string.h>
#include <limits.h>
#define GRAVITY 9.80665f
#define FRESH_MS 2500U
static uint32_t elapsed(uint32_t now,uint32_t then) { return now-then; }
static bool due(uint32_t now,uint32_t target) { return (int32_t)(now-target)>=0; }
static int16_t short_value(float f) { return (int16_t)fmaxf(-32767.0f,fminf(32767.0f,f)); }
rg_policy_t rg_supervision_defaults(void) {
    rg_policy_t p={1,0,3000,200,3000,5000,600000,3,1,true}; return p;
}
static void start_event(rg_supervision_t *s,rg_event_t *e,rg_event_kind_t kind,uint32_t now) {
    memset(e,0,sizeof(*e));e->boot=s->boot;e->seq=++s->seq;e->kind=kind;
    e->policy_version=s->policy.version;e->start_ms=now;e->qualified_ms=now;e->end_ms=now;
    e->gps_valid=s->gps_valid&&elapsed(now,s->gps_at)<=FRESH_MS;
    e->gps_age_ms=e->gps_valid?(uint16_t)elapsed(now,s->gps_at):0;
    e->lat_e7=e->gps_valid?s->lat:INT32_MIN;e->lng_e7=e->gps_valid?s->lng:INT32_MIN;
    e->before_valid=e->gps_valid;e->before_milli_kph=s->speed;
    e->captured_at_ms=s->utc_valid?s->utc_ms+elapsed(now,s->gps_at):0;
    e->limit_milli_kph=s->policy.limit_milli_kph;
}
void rg_supervision_init(rg_supervision_t *s,uint64_t boot,rg_emit_fn emit,rg_capture_fn capture,rg_alert_fn alert,void *context) {
    memset(s,0,sizeof(*s));s->policy=rg_supervision_defaults();s->boot=boot;
    s->emit=emit;s->capture=capture;s->alert=alert;s->context=context;
}
bool rg_supervision_policy(rg_supervision_t *s,const rg_policy_t *p,uint32_t now) {
    if(!p||p->limit_milli_kph>80000U||p->brake_milli_ms2<1000U||p->brake_milli_ms2>12000U||
       p->brake_hold_ms<100U||p->brake_hold_ms>2000U||p->speeding_hold_ms<1000U||p->speeding_hold_ms>30000U||
       p->recovery_ms<1000U||p->recovery_ms>30000U||p->window_ms<60000U||p->window_ms>3600000U||
       p->threshold<2U||p->threshold>RG_PENDING||p->forward_axis==0||p->forward_axis < -3||p->forward_axis>3) return false;
    if(s->active&&s->policy.version!=p->version) {
        s->speed_event.end_ms=now;s->speed_event.partial=true;
        if(s->emit)s->emit(s->context,&s->speed_event,false);
        s->active=false;
    }
    if(s->policy.version!=p->version||s->policy.limit_milli_kph!=p->limit_milli_kph||s->policy.enabled!=p->enabled) {s->candidate=false;s->recovering=false;}
    if(s->policy.forward_axis!=p->forward_axis) {s->calibrated=false;s->cal_count=0;memset(s->cal_sum,0,sizeof s->cal_sum);}
    s->policy=*p;return true;
}
void rg_supervision_roads(rg_supervision_t *s,const rg_road_t *roads,uint8_t count) {
    if(count>RG_ROADS)count=RG_ROADS;
    /* A policy poll must not re-arm an approach already occupied by the rider. */
    for(unsigned i=0;i<RG_ROADS;i++) {
        rg_road_t next={0};if(i<count)next=roads[i];
        if(s->roads[i].id!=next.id||s->roads[i].lat_e7!=next.lat_e7||s->roads[i].lng_e7!=next.lng_e7) {
            s->road_inside[i]=false;s->road_left[i]=0;
        }
        s->roads[i]=next;
    }
}
static void complete_speed(rg_supervision_t *s,uint32_t now) {
    rg_event_t e=s->speed_event;e.end_ms=now;e.partial=s->gps_gap;
    e.after_valid=s->gps_valid;e.after_milli_kph=s->speed;
    unsigned n=0;
    for(unsigned i=0;i<s->pending_count;i++)
        if(elapsed(e.qualified_ms,s->pending[i].qualified_ms)<=s->policy.window_ms)s->pending[n++]=s->pending[i];
    s->pending_count=(uint8_t)n;
    n=0;
    for(unsigned i=0;i<s->occurrence_count;i++)if(elapsed(e.qualified_ms,s->occurrence_at[i])<=s->policy.window_ms)s->occurrence_at[n++]=s->occurrence_at[i];
    /* Only the most recent threshold occurrences are needed to decide the gate. */
    if(n>=s->policy.threshold){memmove(s->occurrence_at,s->occurrence_at+1,(n-1U)*sizeof(uint32_t));n--;}
    s->occurrence_at[n++]=e.qualified_ms;s->occurrence_count=(uint8_t)n;
    s->speed_events++;
    /* Always persist locally; release for upload only when the window qualifies. */
    if(s->emit)s->emit(s->context,&e,false);
    if(s->occurrence_count>=s->policy.threshold&&s->emit) {
        if(!s->group_seq){s->group_boot=e.boot;s->group_seq=e.seq;}
        for(unsigned i=0;i<s->pending_count;i++){
            s->pending[i].group_boot=s->group_boot;s->pending[i].group_seq=s->group_seq;s->emit(s->context,&s->pending[i],true);
        }
        e.group_boot=s->group_boot;e.group_seq=s->group_seq;
        s->emit(s->context,&e,true);s->pending_count=0;
    } else if(s->pending_count<RG_PENDING){s->group_seq=0;s->pending[s->pending_count++]=e;}
    else s->dropped_events++;
    s->active=false;s->candidate=false;s->recovering=false;s->gps_gap=false;
}
void rg_supervision_restore(rg_supervision_t*s,const rg_event_t*event,uint32_t now,int64_t utc_now,bool released){
    if(event->kind!=RG_OVERSPEED||event->policy_version!=s->policy.version||event->captured_at_ms<=0)return;
    int64_t age=utc_now-event->captured_at_ms;if(age<0||age>s->policy.window_ms)return;
    if(s->occurrence_count>=s->policy.threshold){memmove(s->occurrence_at,s->occurrence_at+1,(s->occurrence_count-1U)*sizeof(uint32_t));s->occurrence_count--;}
    s->occurrence_at[s->occurrence_count++]=now-(uint32_t)age;
    if(!released&&s->pending_count<RG_PENDING){rg_event_t copy=*event;copy.qualified_ms=now-(uint32_t)age;s->pending[s->pending_count++]=copy;}
    if(released&&event->group_seq){s->group_boot=event->group_boot;s->group_seq=event->group_seq;}
}
void rg_supervision_gps(rg_supervision_t *s,uint32_t now,bool valid,uint32_t speed,int32_t lat,int32_t lng,bool utc_valid,int64_t utc) {
    bool continuous=s->gps_valid&&elapsed(now,s->gps_at)<=1500U;
    if(!continuous&&!s->active)s->candidate=false;
    if(s->gps_valid&&elapsed(now,s->gps_at)>=500U){s->prior_speed=s->speed;s->prior_at=s->gps_at;s->prior_valid=true;}
    s->gps_valid=valid&&speed<=150000U&&lat>=-900000000&&lat<=900000000&&lng>=-1800000000&&lng<=1800000000;
    s->gps_at=now;s->speed=speed;s->lat=lat;s->lng=lng;s->utc_valid=utc_valid;s->utc_ms=utc;
    if(!s->gps_valid){s->candidate=false;s->recovering=false;if(s->active)s->gps_gap=true;return;}
    if(!s->policy.enabled)return;
    if(s->policy.limit_milli_kph&&s->policy.version) {
        if(speed>s->policy.limit_milli_kph) {
            s->recovering=false;
            if(!s->active) {
                if(!s->candidate){s->candidate=true;s->speed_since=now;s->candidate_peak=speed;}
                if(speed>s->candidate_peak)s->candidate_peak=speed;
                if(elapsed(now,s->speed_since)>=s->policy.speeding_hold_ms) {
                    start_event(s,&s->speed_event,RG_OVERSPEED,now);
                    s->speed_event.start_ms=s->speed_since;
                    s->speed_event.peak_milli_kph=s->candidate_peak;
                    s->speed_event.qualified_ms=now;s->active=true;s->gps_gap=false;s->last_alert=now;
                    if(s->alert)s->alert(s->context,RG_OVERSPEED);
                }
            }
            if(s->active) {
                if(speed>s->speed_event.peak_milli_kph)s->speed_event.peak_milli_kph=speed;
                if(elapsed(now,s->last_alert)>=10000U){s->last_alert=now;if(s->alert)s->alert(s->context,RG_OVERSPEED);}
            }
        } else if(s->active) {
            if(!s->recovering||!continuous){s->recovering=true;s->recovery_since=now;}
            if(elapsed(now,s->recovery_since)>=s->policy.recovery_ms)complete_speed(s,now);
        } else s->candidate=false;
    }
    for(unsigned i=0;i<RG_ROADS;i++) {
        rg_road_t *r=&s->roads[i];if(!r->enabled||!r->id||r->radius_m<10||r->radius_m>300)continue;
        double north=((double)lat-r->lat_e7)*0.01113195;
        double east=((double)lng-r->lng_e7)*0.01113195*cos((double)lat*1e-7*0.01745329252);
        double distance=sqrt(north*north+east*east);
        if(distance<=r->radius_m) {
            s->road_left[i]=0;
            if(!s->road_inside[i]) {
                s->road_inside[i]=true;rg_event_t e;start_event(s,&e,RG_INTERSECTION,now);e.intersection_id=r->id;s->road_events++;
                if(s->emit)s->emit(s->context,&e,true);
                if(s->capture)s->capture(s->context,e.boot,e.seq,e.kind,3);
            }
        } else if(distance>r->radius_m+20U&&s->road_inside[i]) {
            if(!s->road_left[i])s->road_left[i]=now;
            if(elapsed(now,s->road_left[i])>=10000U)s->road_inside[i]=false;
        }
    }
}
void rg_supervision_tick(rg_supervision_t *s,uint32_t now) {
    if(s->gps_valid&&elapsed(now,s->gps_at)>FRESH_MS) {
        s->gps_valid=false;s->candidate=false;s->recovering=false;if(s->active)s->gps_gap=true;
    }
    if(s->brake_pending&&due(now,s->brake_post_until)) {
        s->brake.end_ms=now;s->brake.after_valid=s->gps_valid;s->brake.after_milli_kph=s->speed;
        if(s->emit)s->emit(s->context,&s->brake,true);
        s->brake_pending=false;
    }
}
void rg_supervision_imu(rg_supervision_t *s,uint32_t now,const float a[3],const float w[3]) {
    for(unsigned i=0;i<3;i++)if(!isfinite(a[i])||!isfinite(w[i]))return;
    uint32_t gap=s->imu_at?elapsed(now,s->imu_at):10U;s->imu_at=now;
    if(gap>30U){s->missed_samples++;s->brake_candidate=false;}
    float amag=sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    float corrected_w[3];
    for(unsigned i=0;i<3;i++)corrected_w[i]=w[i]-(s->calibrated?s->gyro_bias[i]:0.0f);
    float gyro=sqrtf(corrected_w[0]*corrected_w[0]+corrected_w[1]*corrected_w[1]+corrected_w[2]*corrected_w[2]);
    bool still=amag>0.94f&&amag<1.06f&&gyro<(s->calibrated?3.0f:15.0f)&&(!s->gps_valid||s->speed<1000U);
    if(!s->calibrated) {
        /* Hold the mounted device still: estimate gyro bias as well as gravity.
         * Reject changing attitude/rates rather than assuming a factory zero bias. */
        if(s->cal_count)for(unsigned i=0;i<3;i++)
            if(fabsf(a[i]-s->cal_accel_ref[i])>0.025f||fabsf(w[i]-s->cal_gyro_ref[i])>1.0f)still=false;
        if(!still||gap>30U){s->cal_count=0;memset(s->cal_sum,0,sizeof s->cal_sum);memset(s->cal_gyro_sum,0,sizeof s->cal_gyro_sum);return;}
        if(s->cal_count==0){
            s->cal_at=now;memset(s->cal_sum,0,sizeof s->cal_sum);memset(s->cal_gyro_sum,0,sizeof s->cal_gyro_sum);
            memcpy(s->cal_accel_ref,a,sizeof s->cal_accel_ref);memcpy(s->cal_gyro_ref,w,sizeof s->cal_gyro_ref);
        }
        for(unsigned i=0;i<3;i++){s->cal_sum[i]+=a[i]*GRAVITY;s->cal_gyro_sum[i]+=w[i];}
        s->cal_count++;
        if(s->cal_count>=200U&&elapsed(now,s->cal_at)>=1990U) {
            for(unsigned i=0;i<3;i++){s->gravity[i]=s->cal_sum[i]/s->cal_count;s->gyro_bias[i]=s->cal_gyro_sum[i]/s->cal_count;}
            s->calibrated=true;
        }
        return;
    }
    float dt=fminf(gap,30U)*0.001f;
    float omega[3]={corrected_w[0]*0.017453293f,corrected_w[1]*0.017453293f,corrected_w[2]*0.017453293f};
    float g[3];memcpy(g,s->gravity,sizeof g);
    s->gravity[0]+=dt*(g[1]*omega[2]-g[2]*omega[1]);
    s->gravity[1]+=dt*(g[2]*omega[0]-g[0]*omega[2]);
    s->gravity[2]+=dt*(g[0]*omega[1]-g[1]*omega[0]);
    float gn=sqrtf(s->gravity[0]*s->gravity[0]+s->gravity[1]*s->gravity[1]+s->gravity[2]*s->gravity[2]);
    float residual=0;
    for(unsigned i=0;i<3;i++)residual+=powf(a[i]*GRAVITY-g[i],2);
    for(unsigned i=0;i<3;i++) {
        if(gn>0.1f)s->gravity[i]*=GRAVITY/gn;
        if(still&&residual<0.25f)s->gravity[i]=0.98f*s->gravity[i]+0.02f*a[i]*GRAVITY;
    }
    int axis=s->policy.forward_axis;unsigned index=(unsigned)(axis<0?-axis:axis)-1U;
    float linear[3];for(unsigned i=0;i<3;i++)linear[i]=a[i]*GRAVITY-s->gravity[i];
    float forward=linear[index]*(axis<0?-1.0f:1.0f),perpendicular=0;
    for(unsigned i=0;i<3;i++)if(i!=index)perpendicular+=linear[i]*linear[i];
    float alpha=dt/(0.016f+dt);s->filtered_forward+=alpha*(forward-s->filtered_forward);
    bool clipped=fabsf(a[0])>1.95f||fabsf(a[1])>1.95f||fabsf(a[2])>1.95f;
    bool clean=!clipped&&perpendicular<16.0f&&gyro<45.0f&&gap<=30U;
    if(++s->trace_divider>=5U) {
        s->trace_divider=0;rg_trace_t tr={now,short_value(s->filtered_forward*1000),short_value(linear[2]*1000),short_value(gyro*10)};
        s->trace[s->trace_head]=tr;s->trace_head=(uint8_t)((s->trace_head+1U)%RG_TRACE);if(s->trace_count<RG_TRACE)s->trace_count++;
        if(s->brake_pending&&s->brake.trace_count<RG_TRACE)s->brake.trace[s->brake.trace_count++]=tr;
    }
    if(!s->policy.enabled||!clean||s->filtered_forward>-(float)s->policy.brake_milli_ms2/1000.0f) {
        s->brake_candidate=false;s->braking=false;return;
    }
    if(s->brake_pending) {
        if(s->filtered_forward*1000<s->brake.min_forward_milli_ms2)s->brake.min_forward_milli_ms2=(int32_t)(s->filtered_forward*1000);
        return;
    }
    if(s->braking||(s->brake_events&&elapsed(now,s->brake_last)<3000U))return;
    if(!s->brake_candidate){s->brake_candidate=true;s->brake_since=now;}
    if(elapsed(now,s->brake_since)<s->policy.brake_hold_ms)return;
    s->braking=true;s->brake_last=now;s->brake_pending=true;s->brake_post_until=now+500U;s->brake_events++;
    start_event(s,&s->brake,RG_BRAKE,now);s->brake.min_forward_milli_ms2=(int32_t)(s->filtered_forward*1000);
    if(s->prior_valid&&elapsed(now,s->prior_at)<3000U){s->brake.before_valid=true;s->brake.before_milli_kph=s->prior_speed;}
    unsigned n=s->trace_count>30U?30U:s->trace_count;
    for(unsigned i=0;i<n;i++)s->brake.trace[i]=s->trace[(s->trace_head+RG_TRACE-n+i)%RG_TRACE];
    s->brake.trace_count=(uint8_t)n;
    if(s->capture)s->capture(s->context,s->brake.boot,s->brake.seq,RG_BRAKE,1);
    if(s->alert)s->alert(s->context,RG_BRAKE);
}
