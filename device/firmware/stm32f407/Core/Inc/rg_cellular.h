#ifndef RG_CELLULAR_H
#define RG_CELLULAR_H
#include "main.h"
#include <stdint.h>
#include <stdbool.h>
typedef uint16_t (*rg_body_fn)(void *,uint32_t,uint8_t *,uint16_t);
typedef void (*rg_http_done_fn)(void *,unsigned,const char *,uint32_t);
typedef struct {
    const char *api_base,*hostname,*token,*certificate,*apn;
    UART_HandleTypeDef *uart;bool enabled;
} rg_cellular_config_t;
void rg_cellular_init(rg_cellular_config_t);
void rg_cellular_rx(void);
void rg_cellular_error(void);
void rg_cellular_task(uint32_t);
bool rg_cellular_ready(void);
bool rg_cellular_request(bool,const char *,const char *,uint32_t,rg_body_fn,rg_http_done_fn,void *);
bool rg_cellular_clock(int64_t *,uint32_t *);
uint32_t rg_cellular_errors(void);
#endif
