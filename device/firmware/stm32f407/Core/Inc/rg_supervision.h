#ifndef RG_SUPERVISION_H
#define RG_SUPERVISION_H
#include <stdbool.h>
#include <stdint.h>
#define RG_HISTORY 32U
#define RG_TRACE 40U
#define RG_ROADS 8U
#define RG_PENDING 32U
typedef enum { RG_BRAKE=1, RG_OVERSPEED=2, RG_INTERSECTION=3 } rg_event_kind_t;
typedef struct {
    uint32_t version, limit_milli_kph, brake_milli_ms2, brake_hold_ms;
    uint32_t speeding_hold_ms, recovery_ms, window_ms, threshold;
    int8_t forward_axis;
    bool enabled;
} rg_policy_t;
typedef struct { uint32_t id; int32_t lat_e7,lng_e7; uint32_t radius_m; bool enabled; } rg_road_t;
typedef struct { uint32_t at; int16_t forward_milli_ms2,vertical_milli_ms2,gyro_deci_dps; } rg_trace_t;
typedef struct {
    uint64_t boot,group_boot; uint32_t seq,group_seq; rg_event_kind_t kind;
    uint32_t policy_version, start_ms, qualified_ms, end_ms, peak_milli_kph, limit_milli_kph;
    uint32_t before_milli_kph, after_milli_kph, intersection_id;
    int32_t lat_e7,lng_e7, min_forward_milli_ms2;
    int64_t captured_at_ms;
    bool gps_valid, before_valid, after_valid, partial; uint16_t gps_age_ms;
    uint8_t trace_count; rg_trace_t trace[RG_TRACE];
} rg_event_t;
typedef void (*rg_emit_fn)(void *,const rg_event_t *,bool upload);
typedef void (*rg_capture_fn)(void *,uint64_t,uint32_t,rg_event_kind_t,uint8_t);
typedef void (*rg_alert_fn)(void *,rg_event_kind_t);
typedef struct {
    rg_policy_t policy; uint64_t boot; uint32_t seq;
    rg_emit_fn emit; rg_capture_fn capture; rg_alert_fn alert; void *context;
    bool calibrated; uint16_t cal_count; float gravity[3],cal_sum[3]; uint32_t imu_at,cal_at;
    float gyro_bias[3],cal_gyro_sum[3],cal_accel_ref[3],cal_gyro_ref[3];
    float filtered_forward; uint32_t brake_since,brake_last,brake_post_until; bool braking,brake_candidate,brake_pending;
    rg_event_t brake;
    rg_trace_t trace[RG_TRACE]; uint8_t trace_head,trace_count,trace_divider;
    bool gps_valid,utc_valid; uint32_t gps_at,speed,prior_speed,prior_at; bool prior_valid;
    int32_t lat,lng; int64_t utc_ms;
    bool candidate,active,recovering,gps_gap; uint32_t speed_since,recovery_since,last_alert,candidate_peak;
    rg_event_t speed_event;
    rg_event_t pending[RG_PENDING]; uint8_t pending_count;
    uint32_t occurrence_at[RG_PENDING]; uint8_t occurrence_count;
    uint64_t group_boot; uint32_t group_seq;
    rg_road_t roads[RG_ROADS]; bool road_inside[RG_ROADS]; uint32_t road_left[RG_ROADS];
    uint32_t missed_samples,dropped_events,brake_events,speed_events,road_events;
} rg_supervision_t;
void rg_supervision_init(rg_supervision_t *,uint64_t,rg_emit_fn,rg_capture_fn,rg_alert_fn,void *);
bool rg_supervision_policy(rg_supervision_t *,const rg_policy_t *,uint32_t);
void rg_supervision_roads(rg_supervision_t *,const rg_road_t *,uint8_t);
void rg_supervision_gps(rg_supervision_t *,uint32_t,bool,uint32_t,int32_t,int32_t,bool,int64_t);
void rg_supervision_imu(rg_supervision_t *,uint32_t,const float accel_g[3],const float gyro_dps[3]);
void rg_supervision_tick(rg_supervision_t *,uint32_t);
rg_policy_t rg_supervision_defaults(void);
void rg_supervision_restore(rg_supervision_t *,const rg_event_t *,uint32_t,int64_t,bool);
#endif
