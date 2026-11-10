#include "rg_cellular.h"
#include "rg_gps.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* One in-flight command; all waits are deadlines advanced by task(), never HAL_Delay. */
static rg_cellular_config_t cfg;
static uint8_t byte,ring[8192],tx[256];static volatile uint16_t head,tail;
static char line[512],reply[4096],command_text[720],url[480];static unsigned line_n,reply_n,read_remaining;
static unsigned terminal,prompt,download,action,status,response_size,errors;
static uint32_t deadline,retry_at,tx_at;static unsigned state,boot_step,request_step,tx_n,tx_offset;
static bool online,request_active,posting,read_mode;
static const char *content_type;static uint32_t body_length,body_offset;
static rg_body_fn body_fn;static rg_http_done_fn done_fn;static void*job_context;
static int64_t utc;static uint32_t clock_at;static bool clock_valid;
static bool expired(uint32_t n,uint32_t d){return (int32_t)(n-d)>=0;}
static void complete(unsigned code){rg_http_done_fn fn=done_fn;void*ctx=job_context;request_active=false;done_fn=NULL;if(fn)fn(ctx,code,reply,reply_n);}
static void fail(uint32_t now){errors++;online=false;state=0;boot_step=0;retry_at=now+30000U;head=tail=0;line_n=0;read_remaining=0;read_mode=false;if(request_active)complete(0);}
void rg_cellular_rx(void){uint16_t next=(uint16_t)((head+1U)&8191U);if(next!=tail){ring[head]=byte;__DMB();head=next;}else{errors++;online=false;}HAL_UART_Receive_IT(cfg.uart,&byte,1);}
void rg_cellular_error(void){errors++;online=false;HAL_UART_Receive_IT(cfg.uart,&byte,1);}
static void observe(uint32_t now){
    line[line_n]=0;
    if(!strcmp(line,"RDY")){fail(now);return;}
    if(!strcmp(line,"OK"))terminal=1;
    if(!strcmp(line,"ERROR")||!strncmp(line,"+CME ERROR",10))terminal=2;
    if(!strcmp(line,"DOWNLOAD"))download=1;
    unsigned method,n;
    if(sscanf(line,"+HTTPACTION: %u,%u,%u",&method,&status,&n)==3&&method==(posting?1U:0U)){action=1;response_size=n;}
    if(read_mode&&sscanf(line,"+HTTPREAD: %u",&n)==1){if(n>=sizeof reply||n!=response_size){fail(now);return;}read_remaining=n;reply_n=0;}
    int64_t fresh;if(!strncmp(line,"+CCLK:",6)&&rg_gps_parse_cclk(line,&fresh)){utc=fresh;clock_at=now;clock_valid=true;}
}
static void pump(uint32_t now){
    unsigned work=0;
    while(tail!=head&&work++<512U){uint8_t c=ring[tail];tail=(uint16_t)((tail+1U)&8191U);
        if(read_remaining){reply[reply_n++]=(char)c;read_remaining--;reply[reply_n]=0;continue;}
        if(c=='>'&&(state==10U)){prompt=1;line_n=0;continue;}
        if(c=='\r')continue;
        if(c=='\n'){if(line_n){observe(now);line_n=0;}}
        else if(line_n+1U<sizeof line)line[line_n++]=(char)c;
        else{fail(now);break;}
    }
}
static bool send_command(const char *text,uint32_t now,uint32_t timeout){
    if(cfg.uart->gState!=HAL_UART_STATE_READY)return false;
    size_t n=strlen(text);if(n>=sizeof command_text)return false;
    if(text!=command_text)memmove(command_text,text,n+1);
    terminal=prompt=download=0;
    tx_at=now;tx_n=0;
    if(HAL_UART_Transmit_IT(cfg.uart,(uint8_t*)command_text,(uint16_t)n)!=HAL_OK)return false;
    deadline=now+timeout;return true;
}
static bool send_data(const uint8_t*data,uint32_t length,uint32_t now){
    if(cfg.uart->gState!=HAL_UART_STATE_READY)return false;
    if(length>sizeof tx)length=sizeof tx;
    memcpy(tx,data,length);tx_n=length;tx_at=now;
    return HAL_UART_Transmit_IT(cfg.uart,tx,(uint16_t)length)==HAL_OK;
}
void rg_cellular_init(rg_cellular_config_t c){cfg=c;online=false;state=0;boot_step=0;head=tail=0;retry_at=1000;errors=0;
    if(c.enabled&&c.uart)HAL_UART_Receive_IT(c.uart,&byte,1);
}
bool rg_cellular_ready(void){return cfg.enabled&&online&&!request_active&&state==20U;}
bool rg_cellular_clock(int64_t*out,uint32_t*at){if(!clock_valid)return false;*out=utc;*at=clock_at;return true;}
uint32_t rg_cellular_errors(void){return errors;}
bool rg_cellular_request(bool post,const char*path,const char*type,uint32_t length,rg_body_fn body,rg_http_done_fn done,void*ctx){
    if(!rg_cellular_ready()||!path||path[0]!='/'||strchr(path,'"')||strchr(path,'\r')||strchr(path,'\n')||length>65536)return false;
    int n=snprintf(url,sizeof url,"%s%s",cfg.api_base,path);if(n<1||(size_t)n>=sizeof url)return false;
    posting=post;body_length=length;body_offset=0;body_fn=body;done_fn=done;job_context=ctx;content_type=type;
    request_step=0;request_active=true;reply_n=0;reply[0]=0;state=30;return true;
}
static bool boot_command(uint32_t now){
    const char *q=NULL;uint32_t timeout=6000;
    switch(boot_step){
    case 0:q="AT\r\n";break;case 1:q="ATE0\r\n";break;case 2:q="AT+CPIN?\r\n";break;
    case 3:q="AT+CEREG?\r\n";break;case 4:q="AT+CGATT=1\r\n";timeout=85000;break;
    case 5:q="AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r\n";break;
    case 6:snprintf(command_text,sizeof command_text,"AT+SAPBR=3,1,\"APN\",\"%s\"\r\n",cfg.apn);q=command_text;break;
    case 7:q="AT+SAPBR=1,1\r\n";timeout=85000;break;
    case 8:q="AT+CCLK?\r\n";break;
    case 9:q="AT+FSCREATE=\"rg_enterprise_ca.pem\"\r\n";break;
    case 10:snprintf(command_text,sizeof command_text,"AT+FSWRITE=\"rg_enterprise_ca.pem\",0,%u,15\r\n",(unsigned)strlen(cfg.certificate));q=command_text;break;
    case 11:q="AT+SSLCFG=\"sslversion\",153,3\r\n";break;
    case 12:q="AT+SSLCFG=\"cacert\",153,\"rg_enterprise_ca.pem\"\r\n";break;
    case 13:q="AT+SSLCFG=\"seclevel\",153,1\r\n";break;
    case 14:snprintf(command_text,sizeof command_text,"AT+SSLCFG=\"hostname\",153,\"%s\"\r\n",cfg.hostname);q=command_text;break;
    case 15:q="AT+SSLCFG=\"ignorelocaltime\",153,0\r\n";break;
    case 16:q="AT+SSLCFG=\"verifymode\",153,0\r\n";break;
    case 17:q="AT+HTTPTERM\r\n";break;case 18:q="AT+HTTPINIT\r\n";break;
    case 19:q="AT+HTTPSSL=1\r\n";break;case 20:q="AT+HTTPPARA=\"CID\",1\r\n";break;
    case 21:q="AT+HTTPPARA=\"TIMEOUT\",30\r\n";break;
    default:online=true;state=20;return true;
    }
    if(!send_command(q,now,timeout))return false;
    state=boot_step==10U?10U:1U;return true;
}
void rg_cellular_task(uint32_t now){
    if(!cfg.enabled||!cfg.uart)return;
    pump(now);
    if(cfg.uart->gState!=HAL_UART_STATE_READY&&(uint32_t)(now-tx_at)>3000U&&tx_n){fail(now);return;}
    if(state==0){if(expired(now,retry_at))boot_command(now);return;}
    if(state==1){if(!terminal){if(expired(now,deadline))fail(now);return;}
        if(terminal==2&&boot_step!=7U&&boot_step!=9U&&boot_step!=17U){fail(now);return;}
        boot_step++;state=0;retry_at=now;return;}
    if(state==10){if(prompt&&cfg.uart->gState==HAL_UART_STATE_READY){tx_offset=0;terminal=0;state=11;deadline=now+17000;}else if(terminal||expired(now,deadline))fail(now);return;}
    if(state==11){unsigned size=(unsigned)strlen(cfg.certificate);if(tx_offset<size&&send_data((const uint8_t*)cfg.certificate+tx_offset,size-tx_offset,now))tx_offset+=tx_n;
        if(tx_offset==size&&cfg.uart->gState==HAL_UART_STATE_READY)state=12;
        if(expired(now,deadline))fail(now);
        return;}
    if(state==12){if(terminal==1){boot_step++;state=0;retry_at=now;}else if(terminal==2||expired(now,deadline))fail(now);return;}
    if(state==20)return;
    if(state==30){
        switch(request_step){
        case 0:snprintf(command_text,sizeof command_text,"AT+HTTPPARA=\"URL\",\"%s\"\r\n",url);break;
        case 1:snprintf(command_text,sizeof command_text,"AT+HTTPPARA=\"USERDATA\",\"X-Device-Token: %s\"\r\n",cfg.token);break;
        case 2:snprintf(command_text,sizeof command_text,"AT+HTTPPARA=\"CONTENT\",\"%s\"\r\n",content_type);break;
        case 3:if(posting){snprintf(command_text,sizeof command_text,"AT+HTTPDATA=%lu,30000\r\n",(unsigned long)body_length);break;}request_step++;return;
        case 4:action=0;response_size=0;snprintf(command_text,sizeof command_text,"AT+HTTPACTION=%u\r\n",posting?1:0);break;
        case 5:read_mode=true;reply_n=0;snprintf(command_text,sizeof command_text,"AT+HTTPREAD\r\n");break;
        default:state=20;read_mode=false;complete(status);return;
        }
        if(send_command(command_text,now,request_step==5?8000:6000))state=request_step==3?32:31;
        return;
    }
    if(state==31){if(!terminal){if(expired(now,deadline))fail(now);return;}
        if(terminal!=1){fail(now);return;}
        if(request_step==4){state=35;deadline=now+45000U;return;}
        request_step++;state=30;return;}
    if(state==32){if(download){body_offset=0;terminal=0;state=33;deadline=now+32000U;}else if(terminal||expired(now,deadline))fail(now);return;}
    if(state==33){
        if(cfg.uart->gState==HAL_UART_STATE_READY&&body_offset<body_length){uint8_t block[256];uint16_t n=body_fn?body_fn(job_context,body_offset,block,(uint16_t)(body_length-body_offset>256?256:body_length-body_offset)):0;
            if(!n){fail(now);return;}if(send_data(block,n,now))body_offset+=n;
        }
        if(body_offset==body_length&&cfg.uart->gState==HAL_UART_STATE_READY){state=34;}if(expired(now,deadline))fail(now);return;
    }
    if(state==34){if(terminal==1){request_step++;state=30;}else if(terminal==2||expired(now,deadline))fail(now);return;}
    if(state==35){if(action){if(response_size>=sizeof reply){fail(now);return;}request_step++;state=30;}else if(expired(now,deadline))fail(now);return;}
}
