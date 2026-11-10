#include "rg_supervision.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static rg_supervision_t s;
static unsigned emitted,uploads,photos,alerts;
static rg_event_t events[64];
static void emit(void *p,const rg_event_t *e,bool up){(void)p;if(up)uploads++;else{assert(emitted<64);events[emitted++]=*e;}if(e->kind!=RG_OVERSPEED&&up)events[emitted++]=*e;}
static void capture(void*p,uint64_t b,uint32_t q,rg_event_kind_t k,uint8_t n){(void)p;(void)b;(void)q;(void)k;photos+=n;}
static void alert(void*p,rg_event_kind_t k){(void)p;(void)k;alerts++;}
static void init(bool configured){emitted=uploads=photos=alerts=0;rg_supervision_init(&s,42,emit,capture,alert,NULL);if(configured){rg_policy_t p=rg_supervision_defaults();p.version=1;p.limit_milli_kph=15000;assert(rg_supervision_policy(&s,&p,0));}}
static void gps(uint32_t t,uint32_t speed){rg_supervision_gps(&s,t,true,speed,340000000,1080000000,true,1794100000000LL+t);}
static void episode(uint32_t t){for(unsigned i=0;i<5;i++)gps(t+i*1000,20000);for(unsigned i=5;i<=10;i++)gps(t+i*1000,12000);}
int main(void){
 init(false);episode(1000);assert(!alerts&&!emitted); /* no inherited 25 km/h */
 init(true);episode(1000);assert(emitted==1&&uploads==0&&alerts==1&&events[0].gps_valid);assert(events[0].peak_milli_kph==20000);assert(events[0].captured_at_ms<1800000000000LL);
 episode(30000);assert(emitted==2&&uploads==0);episode(60000);assert(emitted==3&&uploads==3);
 init(true);episode(1000);episode(700000);episode(1400000);assert(!uploads);
 init(true);for(unsigned t=1000;t<=30000;t+=1000)gps(t,20000);assert(s.active&&s.speed_events==0&&alerts>=2);for(unsigned t=31000;t<=36000;t+=1000)gps(t,10000);assert(s.speed_events==1&&emitted==1);
 init(true);gps(1000,20000);gps(2000,20000);rg_supervision_gps(&s,3000,false,0,INT32_MIN,INT32_MIN,false,0);gps(4000,20000);gps(5000,20000);assert(!s.active);gps(6000,20000);gps(7000,20000);assert(s.active);
 rg_supervision_tick(&s,10000);assert(!s.gps_valid&&s.active);gps(11000,12000);gps(12000,12000);gps(13000,12000);gps(14000,12000);gps(15000,12000);gps(16000,12000);assert(emitted==1&&events[0].partial);
 init(true);rg_policy_t p=s.policy;p.version=2;p.limit_milli_kph=0;assert(rg_supervision_policy(&s,&p,0));episode(1000);assert(!alerts&&!emitted);
 p.forward_axis=0;assert(!rg_supervision_policy(&s,&p,0));
 init(true);rg_road_t road={1,340000000,1080000000,60,true};rg_supervision_roads(&s,&road,1);gps(1000,10000);gps(2000,10000);rg_supervision_roads(&s,&road,1);gps(3000,10000);assert(photos==3&&s.road_events==1&&events[0].kind==RG_INTERSECTION);
 init(true);float a[3]={0,0,1},w[3]={0,0,0};for(unsigned t=10;t<=2500;t+=10)rg_supervision_imu(&s,t,a,w);assert(s.calibrated);
 a[0]=-0.45f;for(unsigned t=2510;t<=2900;t+=10){rg_supervision_imu(&s,t,a,w);rg_supervision_tick(&s,t);}assert(photos==1&&s.brake_events==1);a[0]=0;for(unsigned t=2910;t<=3600;t+=10){rg_supervision_imu(&s,t,a,w);rg_supervision_tick(&s,t);}assert(uploads==1&&events[0].kind==RG_BRAKE&&events[0].trace_count>20);
 init(true);a[0]=0;for(unsigned t=10;t<=2500;t+=10)rg_supervision_imu(&s,t,a,w);a[0]=-.5f;a[2]=1.8f;for(unsigned t=2510;t<=3000;t+=10)rg_supervision_imu(&s,t,a,w);assert(!photos); /* pothole / vertical shock */
 init(true);a[0]=0;a[2]=1;for(unsigned t=10;t<=2500;t+=10)rg_supervision_imu(&s,t,a,w);a[0]=-.5f;w[2]=90;for(unsigned t=2510;t<=3000;t+=10)rg_supervision_imu(&s,t,a,w);assert(!photos); /* fast turning */
 init(true);for(unsigned i=0;i<5;i++)gps(UINT32_MAX-2000U+i*1000U,20000);assert(s.active); /* tick wrap */
 /* Reproduce the connected board's stationary gyro zero bias. */
 init(true);a[0]=-.065f;a[1]=-.041f;a[2]=1.0f;w[0]=-6.5f;w[1]=2.1f;w[2]=.9f;
 for(unsigned t=10;t<=2500;t+=10)rg_supervision_imu(&s,t,a,w);
 assert(s.calibrated&&fabsf(s.gyro_bias[0]+6.5f)<.01f);
 for(unsigned t=2510;t<=10000;t+=10)rg_supervision_imu(&s,t,a,w);
 assert(!s.brake_events&&fabsf(s.filtered_forward)<.05f);
 a[0]-=.45f;for(unsigned t=10010;t<=10400;t+=10)rg_supervision_imu(&s,t,a,w);
 assert(s.brake_events==1&&photos==1);
 /* Motion during the calibration interval must keep it unarmed. */
 init(true);a[0]=0;a[1]=0;a[2]=1;
 for(unsigned t=10;t<=4000;t+=10){w[0]=(t/100)%2?6.0f:-6.0f;rg_supervision_imu(&s,t,a,w);}
 assert(!s.calibrated&&!s.brake_events);
 printf("Supervision scenarios passed; engine RAM=%u bytes\n",(unsigned)sizeof s);return 0;
}
