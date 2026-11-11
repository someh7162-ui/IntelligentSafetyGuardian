/* Enterprise runtime; keeps 100 Hz IMU work ahead of bounded I/O tasks. */
#include "rg_supervision.h"
#include "rg_journal.h"
#include "rg_w25q.h"
#include "rg_cellular.h"
#include "rg_enterprise_codec.h"
#include "rg_json.h"
#include "rg_enterprise.h"
#include "enterprise_config.h"
#include "usbd_cdc_if.h"
#include <math.h>
#include <stdarg.h>
volatile BringupDiagnostics bringup_diag={.magic=0x52475612,.version=0x00020002};
volatile RgEnterpriseDiagnostics enterprise_diag={.magic=0x52474532,.version=0x00020002};
UART_HandleTypeDef huart3,huart5;
static rg_supervision_t supervision __attribute__((section(".ccmram")));
static rg_journal_t journal __attribute__((section(".ccmram")));
static rg_event_t jobs[8];static bool jobs_ready[8];static uint8_t jobs_in,jobs_out,jobs_count;static bool job_writing;
typedef struct {uint16_t photo,part,parts,length;uint32_t total,capture_tick;int64_t captured_ms;} PhotoHeader;
static struct {uint64_t boot;uint32_t seq;uint8_t remaining,photo;rg_event_kind_t kind;} camera_jobs[4];
static unsigned camera_in,camera_out,camera_count;static bool capture_active,photo_storing;static uint32_t capture_at,next_capture,photo_offset;
static uint16_t photo_part,photo_parts;static uint8_t photo_block[sizeof(PhotoHeader)+2048];
static rg_event_t stored_event;
typedef struct {uint32_t magic;rg_policy_t policy;rg_road_t roads[RG_ROADS];uint8_t count;} SavedConfig;
static SavedConfig saved_config;static bool config_pending,config_writing;
static uint32_t next_imu,next_status,next_policy,next_telemetry,upload_after;
static uint32_t imu_retry_at;static unsigned imu_failures;
static char http_body[4608],http_path[360],ack_id[48];static uint32_t http_length;
static int upload_kind,upload_slot=-1;static bool request_busy;static uint32_t upload_photo_total;static uint64_t photo_boot;static uint32_t photo_seq;static uint16_t photo_number;
static uint8_t voice_pending,voice_frame[10],voice_rx;static uint32_t voice_at;
static rg_gps_fix_t last_gps;static int64_t network_utc;static uint32_t network_at;
static uint64_t boot_id;
static bool local_capture_active;static uint32_t local_capture_at;
static int64_t utc_at(uint32_t now){
    if(last_gps.utc_valid&&(uint32_t)(now-last_gps.receipt_tick_ms)<3000)return last_gps.unix_ms+(uint32_t)(now-last_gps.receipt_tick_ms);
    if(network_utc&&(uint32_t)(now-network_at)<3600000U)return network_utc+(uint32_t)(now-network_at);
    int64_t utc;uint32_t at;if(rg_cellular_clock(&utc,&at)&&(uint32_t)(now-at)<3600000U)return utc+(uint32_t)(now-at);
    return 0;
}
void USART3_IRQHandler(void){HAL_UART_IRQHandler(&huart3);}
void UART5_IRQHandler(void){HAL_UART_IRQHandler(&huart5);}
void Bringup_RxComplete(UART_HandleTypeDef*u){if(u==&huart3)rg_cellular_rx();else if(u==&huart5){bringup_diag.mp3_bytes++;HAL_UART_Receive_IT(u,&voice_rx,1);}}
void Bringup_RxError(UART_HandleTypeDef*u){if(u==&huart3)rg_cellular_error();else if(u==&huart5){enterprise_diag.voice_errors++;HAL_UART_Receive_IT(u,&voice_rx,1);}}
static void ports_init(void){
    __HAL_RCC_GPIOD_CLK_ENABLE();__HAL_RCC_GPIOC_CLK_ENABLE();__HAL_RCC_USART3_CLK_ENABLE();__HAL_RCC_UART5_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_8|GPIO_PIN_9;g.Mode=GPIO_MODE_AF_PP;g.Pull=GPIO_PULLUP;g.Speed=GPIO_SPEED_FREQ_VERY_HIGH;g.Alternate=GPIO_AF7_USART3;HAL_GPIO_Init(GPIOD,&g);
    g.Pin=GPIO_PIN_12;g.Alternate=GPIO_AF8_UART5;HAL_GPIO_Init(GPIOC,&g);g.Pin=GPIO_PIN_2;HAL_GPIO_Init(GPIOD,&g);
    huart3.Instance=USART3;huart3.Init=(UART_InitTypeDef){115200,UART_WORDLENGTH_8B,UART_STOPBITS_1,UART_PARITY_NONE,UART_MODE_TX_RX,UART_HWCONTROL_NONE,UART_OVERSAMPLING_16};
    huart5.Instance=UART5;huart5.Init=huart3.Init;huart5.Init.BaudRate=9600;
    if(HAL_UART_Init(&huart3)!=HAL_OK||HAL_UART_Init(&huart5)!=HAL_OK)Error_Handler();
    HAL_NVIC_SetPriority(USART3_IRQn,2,0);HAL_NVIC_EnableIRQ(USART3_IRQn);HAL_NVIC_SetPriority(UART5_IRQn,3,0);HAL_NVIC_EnableIRQ(UART5_IRQn);
    HAL_UART_Receive_IT(&huart5,&voice_rx,1);
}
static uint64_t random_boot(void){
    __HAL_RCC_RNG_CLK_ENABLE();RNG->CR|=RNG_CR_RNGEN;uint64_t result=0;
    for(unsigned i=0;i<2;i++){uint32_t at=HAL_GetTick();while(!(RNG->SR&RNG_SR_DRDY))if((uint32_t)(HAL_GetTick()-at)>20)return 0;
        if(RNG->SR&(RNG_SR_CECS|RNG_SR_SECS))return 0;
        result=(result<<32)|RNG->DR;}
    return result;
}
static bool imu_recover(void){
    /* A debugger reset can interrupt an I2C transaction while the sensor stays powered.
     * Release the dedicated MPU bus before reinitializing; never drive it push-pull. */
    HAL_I2C_DeInit(&hi2c1);
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_8|GPIO_PIN_9;g.Mode=GPIO_MODE_OUTPUT_OD;
    g.Pull=GPIO_PULLUP;g.Speed=GPIO_SPEED_FREQ_LOW;HAL_GPIO_Init(GPIOB,&g);
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8|GPIO_PIN_9,GPIO_PIN_SET);HAL_Delay(1);
    for(unsigned i=0;i<9&&HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_9)==GPIO_PIN_RESET;i++){
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_RESET);HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_SET);HAL_Delay(1);
    }
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8|GPIO_PIN_9,GPIO_PIN_RESET);HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_SET);HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,GPIO_PIN_SET);HAL_Delay(1);
    __HAL_RCC_I2C1_FORCE_RESET();__HAL_RCC_I2C1_RELEASE_RESET();MX_I2C1_Init();
    return MPU6050_Init(&hi2c1)==HAL_OK;
}
static void alert_event(void*ctx,rg_event_kind_t kind){(void)ctx;voice_pending=kind==RG_OVERSPEED?2:1;}
static void voice_task(uint32_t now){
    if(!voice_pending||huart5.gState!=HAL_UART_STATE_READY||now-voice_at<1200)return;
    /* DFPlayer /MP3/0001.mp3: brake; /MP3/0002.mp3: speeding. Command 0x12, checked checksum. */
    dfplayer_query(0x12,voice_frame);voice_frame[6]=voice_pending;uint16_t sum=0;for(unsigned i=1;i<=6;i++)sum+=voice_frame[i];sum=(uint16_t)(0U-sum);voice_frame[7]=sum>>8;voice_frame[8]=(uint8_t)sum;
    if(HAL_UART_Transmit_IT(&huart5,voice_frame,10)==HAL_OK){enterprise_diag.voice_sent++;voice_at=now;voice_pending=0;}else enterprise_diag.voice_errors++;
}
static void capture_request(void*ctx,uint64_t boot,uint32_t seq,rg_event_kind_t kind,uint8_t count){
    (void)ctx;if(camera_count>=4){enterprise_diag.photo_dropped+=count;return;}
    camera_jobs[camera_in].boot=boot;camera_jobs[camera_in].seq=seq;camera_jobs[camera_in].kind=kind;
    camera_jobs[camera_in].remaining=count;camera_jobs[camera_in].photo=0;camera_in=(camera_in+1U)%4U;camera_count++;
}
static void emit_event(void*ctx,const rg_event_t*e,bool ready){
    /* An unprovisioned device keeps detection active without claiming durable records. */
    if(!journal.writable)return;
    (void)ctx;if(!boot_id||jobs_count>=8){enterprise_diag.storage_full++;return;}
    uint16_t part=ready?1:0;if(rg_journal_find(&journal,RG_JOURNAL_EVENT,e->boot,e->seq,part)>=0)return;
    rg_event_t value=*e;
    if(ready&&e->kind==RG_OVERSPEED){int old=rg_journal_find(&journal,RG_JOURNAL_EVENT,e->boot,e->seq,0);
        if(old>=0){rg_journal_read(&journal,(unsigned)old,&value,sizeof value);value.group_boot=e->group_boot;value.group_seq=e->group_seq;}}
    /* Merge queued local+release callbacks without changing the immutable event body. */
    for(unsigned n=0;n<jobs_count;n++){unsigned i=(jobs_out+n)%8;if(jobs[i].boot==e->boot&&jobs[i].seq==e->seq){
        if(ready){jobs_ready[i]=true;jobs[i].group_boot=e->group_boot;jobs[i].group_seq=e->group_seq;}return;}}
    jobs[jobs_in]=value;jobs_ready[jobs_in]=ready;jobs_in=(jobs_in+1U)%8U;jobs_count++;
}
static bool reclaim_record(void*ctx,const rg_journal_header_t*h,unsigned slot){
    (void)ctx;if(h->type!=RG_JOURNAL_EVENT)return true;
    rg_event_t e;if(!rg_journal_read(&journal,slot,&e,sizeof e))return false;
    for(unsigned i=0;i<journal.slots;i++)if(journal.occupied[i]==1&&journal.index[i].type==RG_JOURNAL_PHOTO&&journal.index[i].boot==h->boot&&journal.index[i].seq==h->seq&&journal.index[i].status!=RG_JOURNAL_ACK)return false;
    if(h->part==0&&rg_journal_find(&journal,RG_JOURNAL_EVENT,h->boot,h->seq,1)>=0)return true;
    int64_t now=utc_at(HAL_GetTick());return now&&e.captured_at_ms>0&&now-e.captured_at_ms>3600000LL;
}
static void storage_task(void){
    rg_journal_task(&journal);if(journal.state)return;
    if(job_writing){job_writing=false;if(journal.result==1){jobs_out=(jobs_out+1U)%8U;jobs_count--;}else enterprise_diag.storage_fault=1;}
    if(config_writing){config_writing=false;if(journal.result==1)config_pending=false;}
    if(!journal.writable||journal.fault)return;
    if(jobs_count){rg_event_t*e=&jobs[jobs_out];if(rg_journal_append(&journal,RG_JOURNAL_EVENT,e->boot,e->seq,jobs_ready[jobs_out]?1:0,e,sizeof *e,jobs_ready[jobs_out]))job_writing=true;return;}
    if(config_pending){if(rg_journal_append(&journal,RG_JOURNAL_CONFIG,0,saved_config.policy.version,0,&saved_config,sizeof saved_config,true))config_writing=true;return;}
    for(unsigned i=0;i<journal.slots;i++)if(journal.occupied[i]==1&&journal.index[i].type==RG_JOURNAL_CONFIG&&journal.index[i].seq<supervision.policy.version&&journal.index[i].status!=RG_JOURNAL_ACK){rg_journal_mark(&journal,i,RG_JOURNAL_ACK);return;}
    if(photo_storing&&camera_count){
        if(photo_offset>=camera_frame.length){photo_storing=false;camera_jobs[camera_out].remaining--;camera_jobs[camera_out].photo++;next_capture=HAL_GetTick()+100;return;}
        unsigned amount=camera_frame.length-photo_offset;if(amount>2048)amount=2048;
        PhotoHeader h={camera_jobs[camera_out].photo,photo_part,photo_parts,(uint16_t)amount,camera_frame.length,capture_at,utc_at(capture_at)};
        memcpy(photo_block,&h,sizeof h);memcpy(photo_block+sizeof h,camera_frame.data+photo_offset,amount);
        uint16_t part=(uint16_t)(h.photo*32U+photo_part);
        if(rg_journal_append(&journal,RG_JOURNAL_PHOTO,camera_jobs[camera_out].boot,camera_jobs[camera_out].seq,part,photo_block,(uint16_t)(sizeof h+amount),true)){
            photo_offset+=amount;photo_part++;
        }else enterprise_diag.storage_full++;
    }
}
static void camera_task(uint32_t now){
    if(!camera_count||photo_storing)return;
    if(!camera_jobs[camera_out].remaining){camera_out=(camera_out+1U)%4U;camera_count--;return;}
    if(!capture_active){if((int32_t)(now-next_capture)<0)return;
        if(OV2640_StartJPEG(&camera)==HAL_OK){capture_at=now;capture_active=true;}else{enterprise_diag.camera_errors++;camera_jobs[camera_out].remaining--;next_capture=now+1100;}return;}
    HAL_StatusTypeDef status=OV2640_PollJPEG(&camera,&camera_frame);if(status==HAL_BUSY)return;capture_active=false;
    if(status!=HAL_OK||camera_frame.length>65536){enterprise_diag.camera_errors++;camera_jobs[camera_out].remaining--;next_capture=now+1100;return;}
    enterprise_diag.camera_ok++;
    if(!journal.writable){
        /* Keep the latest event JPEG in RAM for local inspection, never imply power-loss persistence. */
        enterprise_diag.volatile_photos++;camera_jobs[camera_out].remaining--;camera_jobs[camera_out].photo++;next_capture=now+100;return;
    }
    photo_offset=0;photo_part=0;photo_parts=(uint16_t)((camera_frame.length+2047)/2048);photo_storing=true;
}
static void local_camera_task(uint32_t now){
    /* Explicit SWD local-photo request: 1=request, 2=busy, 0=idle.
     * This path neither manufactures safety events nor writes/uploads photos. */
    if(!local_capture_active){
        if(enterprise_diag.camera_command!=1||camera_count||capture_active||photo_storing)return;
        enterprise_diag.camera_result=0;enterprise_diag.camera_frame_bytes=0;
        HAL_StatusTypeDef status=OV2640_StartJPEG(&camera);
        if(status!=HAL_OK){enterprise_diag.camera_result=2;enterprise_diag.camera_command=0;return;}
        local_capture_active=true;local_capture_at=now;enterprise_diag.camera_command=2;return;
    }
    HAL_StatusTypeDef status=OV2640_PollJPEG(&camera,&camera_frame);
    if(status==HAL_BUSY)return;
    local_capture_active=false;enterprise_diag.camera_capture_ms=HAL_GetTick()-local_capture_at;
    if(status==HAL_OK){enterprise_diag.camera_ok++;enterprise_diag.volatile_photos++;enterprise_diag.camera_frame_bytes=camera_frame.length;enterprise_diag.camera_result=1;}
    else{enterprise_diag.camera_errors++;enterprise_diag.camera_result=2;}
    __DMB();enterprise_diag.camera_command=0;
}
static uint16_t body_read(void*ctx,uint32_t offset,uint8_t*out,uint16_t max){
    (void)ctx;
    if(upload_kind!=3){if(offset>=http_length)return 0;uint32_t n=http_length-offset;if(n>max)n=max;memcpy(out,http_body+offset,n);return (uint16_t)n;}
    unsigned part=offset/2048U;int slot=rg_journal_find(&journal,RG_JOURNAL_PHOTO,photo_boot,photo_seq,(uint16_t)(photo_number*32U+part));
    if(slot<0)return 0;
    PhotoHeader h;
    uint32_t at=journal.base+(unsigned)slot*4096U+sizeof(rg_journal_header_t);
    if(!rg_w25q_read(NULL,at,&h,sizeof h))return 0;
    unsigned within=offset%2048U;if(within>=h.length)return 0;unsigned n=h.length-within;if(n>max)n=max;
    if(!rg_w25q_read(NULL,at+sizeof h+within,out,n))return 0;
    return (uint16_t)n;
}
static int ack_slots[33];static unsigned ack_count,ack_next;
static void http_done(void*ctx,unsigned code,const char*response,uint32_t length){
    (void)ctx;request_busy=false;upload_after=HAL_GetTick()+2000;
    if(code!=200){enterprise_diag.upload_errors++;return;}
    if(upload_kind==1){
        rg_policy_t p;rg_road_t roads[RG_ROADS];uint8_t count;
        if(rg_policy_json(response,length,&p,roads,&count)&&p.version>=supervision.policy.version&&rg_supervision_policy(&supervision,&p,HAL_GetTick())){
            rg_supervision_roads(&supervision,roads,count);enterprise_diag.policy_version=p.version;
            saved_config.magic=0x52474332;saved_config.policy=p;saved_config.count=count;memcpy(saved_config.roads,roads,sizeof roads);
            config_pending=rg_journal_find(&journal,RG_JOURNAL_CONFIG,0,p.version,0)<0;
        }else enterprise_diag.upload_errors++;
        return;
    }
    if(upload_kind==4)return;
    if(!rg_ack_json(response,length,ack_id,upload_kind==3)){enterprise_diag.upload_errors++;return;}
    ack_next=0;ack_count=0;
    if(upload_kind==2){ack_slots[ack_count++]=upload_slot;enterprise_diag.event_acks++;}
    else{
        for(unsigned s=0;s<journal.slots;s++)if(journal.occupied[s]==1&&journal.index[s].type==RG_JOURNAL_PHOTO&&journal.index[s].boot==photo_boot&&journal.index[s].seq==photo_seq&&journal.index[s].part/32U==photo_number)ack_slots[ack_count++]=(int)s;
        enterprise_diag.photo_acks++;
    }
}
static void ack_task(void){if(ack_next>=ack_count||journal.state)return;if(rg_journal_mark(&journal,(unsigned)ack_slots[ack_next],RG_JOURNAL_ACK))ack_next++;}
static void upload_task(uint32_t now){
    if(request_busy||!rg_cellular_ready()||(int32_t)(now-upload_after)<0||ack_next<ack_count||journal.state)return;
    if((int32_t)(now-next_policy)>=0){
        snprintf(http_path,sizeof http_path,"/supervision-policy?deviceId=%s",RG_DEVICE_ID);upload_kind=1;
        if(rg_cellular_request(false,http_path,"application/json",0,NULL,http_done,NULL)){request_busy=true;next_policy=now+60000;}return;
    }
    if(!journal.writable)return;
    for(unsigned slot=0;slot<journal.slots;slot++)if(journal.occupied[slot]==1&&journal.index[slot].status==RG_JOURNAL_READY&&journal.index[slot].type==RG_JOURNAL_EVENT&&journal.index[slot].part==1){
        if(!rg_journal_read(&journal,slot,&stored_event,sizeof stored_event))continue;
        int n=rg_event_json(http_body,sizeof http_body,RG_DEVICE_ID,&stored_event);if(n<1){enterprise_diag.upload_errors++;continue;}
        http_length=(uint32_t)n;upload_kind=2;upload_slot=(int)slot;rg_event_id(ack_id,sizeof ack_id,stored_event.boot,stored_event.seq);
        if(rg_cellular_request(true,"/events","application/json",http_length,body_read,http_done,NULL))request_busy=true;
        return;
    }
    for(unsigned slot=0;slot<journal.slots;slot++)if(journal.occupied[slot]==1&&journal.index[slot].status==RG_JOURNAL_READY&&journal.index[slot].type==RG_JOURNAL_PHOTO&&journal.index[slot].part%32U==0){
        rg_journal_header_t*h=&journal.index[slot];int event=rg_journal_find(&journal,RG_JOURNAL_EVENT,h->boot,h->seq,1);
        if(event<0||journal.index[event].status!=RG_JOURNAL_ACK)continue;
        PhotoHeader ph;if(!rg_w25q_read(NULL,journal.base+slot*4096U+sizeof *h,&ph,sizeof ph)||ph.parts>32||!ph.parts||ph.total>65536)continue;
        bool complete=true;for(unsigned n=0;n<ph.parts;n++)if(rg_journal_find(&journal,RG_JOURNAL_PHOTO,h->boot,h->seq,(uint16_t)(ph.photo*32U+n))<0)complete=false;
        if(!complete)continue;
        char event_id[40];rg_event_id(event_id,sizeof event_id,h->boot,h->seq);snprintf(ack_id,sizeof ack_id,"photo-%u",ph.photo);
        snprintf(http_path,sizeof http_path,"/event-image?deviceId=%s&eventId=%s&photoId=%s",RG_DEVICE_ID,event_id,ack_id);
        if(ph.captured_ms>0){size_t n=strlen(http_path);snprintf(http_path+n,sizeof http_path-n,"&capturedAtMs=%lld",(long long)ph.captured_ms);}
        upload_kind=3;photo_boot=h->boot;photo_seq=h->seq;photo_number=ph.photo;upload_photo_total=ph.total;
        if(rg_cellular_request(true,http_path,"image/jpeg",upload_photo_total,body_read,http_done,NULL))request_busy=true;
        return;
    }
    int64_t stamp=utc_at(now);
    if(stamp&&(int32_t)(now-next_telemetry)>=0){
        bool valid=supervision.gps_valid;char lat[32]="null",lng[32]="null";if(valid){snprintf(lat,sizeof lat,"%.7f",supervision.lat*1e-7);snprintf(lng,sizeof lng,"%.7f",supervision.lng*1e-7);}
        http_length=(uint32_t)snprintf(http_body,sizeof http_body,"{\"deviceId\":\"%s\",\"sampleId\":\"%08lx-%lu\",\"capturedAtMs\":%lld,\"gpsValid\":%s,\"locationSource\":\"%s\",\"latitude\":%s,\"longitude\":%s,\"speedKph\":%.3f}",RG_DEVICE_ID,(unsigned long)boot_id,(unsigned long)now,(long long)stamp,valid?"true":"false",valid?"GPS":"NONE",lat,lng,valid?supervision.speed*.001:0.0);
        upload_kind=4;if(rg_cellular_request(true,"/telemetry","application/json",http_length,body_read,http_done,NULL)){request_busy=true;next_telemetry=now+3000;}
    }
}
static void restore_events(uint32_t now){
    if(enterprise_diag.restore_done)return;
    int64_t utc=utc_at(now);if(!utc||!journal.writable)return;
    /* Restore rolling-window records once time and policy are known, in captured order. */
    int slots[RG_PENDING];unsigned count=0;
    for(unsigned i=0;i<journal.slots;i++)if(journal.occupied[i]==1&&journal.index[i].type==RG_JOURNAL_EVENT){
        rg_journal_header_t*h=&journal.index[i];if(h->part==0&&rg_journal_find(&journal,RG_JOURNAL_EVENT,h->boot,h->seq,1)>=0)continue;
        rg_event_t e;if(rg_journal_read(&journal,i,&e,sizeof e)&&e.kind==RG_OVERSPEED&&e.captured_at_ms>0&&utc-e.captured_at_ms>=0&&utc-e.captured_at_ms<=supervision.policy.window_ms&&count<RG_PENDING)slots[count++]=(int)i;
    }
    for(unsigned i=0;i<count;i++)for(unsigned k=i+1;k<count;k++){rg_event_t a,b;rg_journal_read(&journal,slots[i],&a,sizeof a);rg_journal_read(&journal,slots[k],&b,sizeof b);if(a.captured_at_ms>b.captured_at_ms){int t=slots[i];slots[i]=slots[k];slots[k]=t;}}
    for(unsigned i=0;i<count;i++){rg_event_t e;rg_journal_read(&journal,slots[i],&e,sizeof e);rg_supervision_restore(&supervision,&e,now,utc,journal.index[slots[i]].part==1);}
    enterprise_diag.restore_done=1;
}
static void mailbox_task(void){
    if(enterprise_diag.read_command!=1)return;
    uint32_t at=enterprise_diag.read_address,n=enterprise_diag.read_length;
    uint8_t bytes[256];if(n>256||!n||!rg_w25q_read(NULL,at,bytes,n))enterprise_diag.read_result=2;
    else{for(unsigned i=0;i<n;i++)enterprise_diag.read_buffer[i]=bytes[i];enterprise_diag.read_result=1;}
    __DMB();enterprise_diag.read_command=0;
}
int main(void){
    HAL_Init();SystemClock_Config();MX_GPIO_Init();MX_DMA_Init();MX_USB_DEVICE_Init();MX_I2C1_Init();MX_I2C2_Init();MX_DCMI_Init();MX_UART4_UART_Init();ports_init();
    boot_id=random_boot();enterprise_diag.identity_fault=!boot_id;
    rg_supervision_init(&supervision,boot_id,emit_event,capture_request,alert_event,NULL);
    char uid[32];snprintf(uid,sizeof uid,"%08lx%08lx%08lx",(unsigned long)HAL_GetUIDw0(),(unsigned long)HAL_GetUIDw1(),(unsigned long)HAL_GetUIDw2());
    bool permit=RG_STORAGE_PROVISIONED&&strlen(RG_STORAGE_BACKUP_SHA256)==64&&!strcmp(uid,RG_STORAGE_UID);
    if(rg_w25q_init(permit)){rg_journal_io_t io=rg_w25q_io();io.reclaim=reclaim_record;rg_journal_init(&journal,io,rg_w25q_capacity()-1048576U,256,permit);}
    else memset(&journal,0,sizeof journal);
    enterprise_diag.flash_jedec=rg_w25q_jedec();enterprise_diag.flash_capacity=rg_w25q_capacity();enterprise_diag.storage_writable=journal.writable;
    uint32_t latest=0;int config_slot=-1;
    for(unsigned i=0;i<journal.slots;i++)if(journal.occupied[i]==1&&journal.index[i].type==RG_JOURNAL_CONFIG&&journal.index[i].length==sizeof saved_config&&journal.index[i].seq>latest){latest=journal.index[i].seq;config_slot=(int)i;}
    if(config_slot>=0&&rg_journal_read(&journal,(unsigned)config_slot,&saved_config,sizeof saved_config)&&saved_config.magic==0x52474332&&saved_config.count<=RG_ROADS&&rg_supervision_policy(&supervision,&saved_config.policy,HAL_GetTick()))rg_supervision_roads(&supervision,saved_config.roads,saved_config.count);
    mpu_ready=MPU6050_Init(&hi2c1)==HAL_OK;
    if(!mpu_ready)mpu_ready=imu_recover();
    enterprise_diag.camera_ready=OV2640_Begin(&camera,&hi2c2,&hdcmi,&hdma_dcmi)==HAL_OK;
    enterprise_diag.camera_pid=((uint32_t)camera.pidh<<8)|camera.pidl;GNSS_Init(&huart4);
    enterprise_diag.camera_width=OV2640_JPEG_WIDTH;enterprise_diag.camera_height=OV2640_JPEG_HEIGHT;
    rg_cellular_config_t config={RG_API_BASE,RG_API_HOST,RG_DEVICE_TOKEN,RG_CA_CERT,RG_APN,&huart3,RG_CELLULAR_ENABLED};rg_cellular_init(config);
    while(1){uint32_t now=HAL_GetTick();enterprise_diag.heartbeat=now;
        if((int32_t)(now-next_imu)>=0){if(next_imu&&now-next_imu>10)enterprise_diag.imu_late++;next_imu=now+10;
            if(mpu_ready&&MPU6050_Read(&mpu_data)==HAL_OK){float a[]={mpu_data.accel_x_g,mpu_data.accel_y_g,mpu_data.accel_z_g};float w[]={mpu_data.gyro_x_dps,mpu_data.gyro_y_dps,mpu_data.gyro_z_dps};rg_supervision_imu(&supervision,now,a,w);enterprise_diag.imu_reads++;imu_failures=0;}
            else{enterprise_diag.imu_errors++;if(++imu_failures>=3){mpu_ready=0;supervision.calibrated=false;supervision.cal_count=0;}}
        }
        if(!mpu_ready&&(int32_t)(now-imu_retry_at)>=0){mpu_ready=imu_recover();imu_retry_at=HAL_GetTick()+2000;}
        while(GNSS_Task(gnss_line,sizeof gnss_line)){rg_gps_fix_t f;if(rg_gps_parse_rmc(gnss_line,now,&f)){last_gps=f;rg_supervision_gps(&supervision,now,f.gps_valid,f.speed_milli_kph,f.latitude_e7,f.longitude_e7,f.utc_valid,f.unix_ms);}}
        rg_supervision_tick(&supervision,now);voice_task(now);
        if(!local_capture_active)camera_task(now);
        local_camera_task(now);storage_task();ack_task();rg_cellular_task(now);restore_events(now);upload_task(now);mailbox_task();
        enterprise_diag.calibrated=supervision.calibrated;enterprise_diag.brake_events=supervision.brake_events;enterprise_diag.speed_events=supervision.speed_events;enterprise_diag.road_events=supervision.road_events;
        enterprise_diag.gps_valid=supervision.gps_valid;enterprise_diag.modem_ready=rg_cellular_ready();enterprise_diag.modem_errors=rg_cellular_errors();enterprise_diag.storage_fault=journal.fault;enterprise_diag.storage_full=journal.full_count;enterprise_diag.storage_queued=jobs_count;
        enterprise_diag.gps_bytes=gnss_rx_bytes;enterprise_diag.gps_errors=gnss_rx_errors;
        if((int32_t)(now-next_status)>=0&&USB_Stream_Connected()){
            static char status[220];extern USBD_HandleTypeDef hUsbDeviceFS;USBD_CDC_HandleTypeDef*cdc=(USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
            if(cdc&&cdc->TxState==0){int n=snprintf(status,sizeof status,"RG_ENTERPRISE GPS=%lu IMU=%lu CAL=%lu BRAKE=%lu SPEED=%lu ROAD=%lu STORE=%lu NET=%lu\r\n",(unsigned long)enterprise_diag.gps_valid,(unsigned long)enterprise_diag.imu_reads,(unsigned long)enterprise_diag.calibrated,(unsigned long)enterprise_diag.brake_events,(unsigned long)enterprise_diag.speed_events,(unsigned long)enterprise_diag.road_events,(unsigned long)journal.writable,(unsigned long)enterprise_diag.modem_ready);CDC_Transmit_FS((uint8_t*)status,(uint16_t)n);next_status=now+3000;}}
    }
}
