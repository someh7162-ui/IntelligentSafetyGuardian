#include "main.h"
#include "rg_w25q.h"
static uint32_t jedec,capacity,base;static bool writes;
static void select(bool active){HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,active?GPIO_PIN_RESET:GPIO_PIN_SET);}
static bool transfer(uint8_t value,uint8_t*out){
    uint32_t start=HAL_GetTick();while(!(SPI3->SR&SPI_SR_TXE))if(HAL_GetTick()-start>2)return false;
    *(volatile uint8_t*)&SPI3->DR=value;
    while(!(SPI3->SR&SPI_SR_RXNE))if(HAL_GetTick()-start>2)return false;
    *out=*(volatile uint8_t*)&SPI3->DR;return true;
}
static bool byte(uint8_t b){uint8_t out;return transfer(b,&out);}
static bool addressed(uint8_t command,uint32_t at){return byte(command)&&byte((uint8_t)(at>>16))&&byte((uint8_t)(at>>8))&&byte((uint8_t)at);}
static bool write_enable(void){select(true);bool ok=byte(6);select(false);return ok;}
bool rg_w25q_init(bool permit){
    __HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_SPI3_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_0;g.Mode=GPIO_MODE_OUTPUT_PP;g.Pull=GPIO_PULLUP;g.Speed=GPIO_SPEED_FREQ_VERY_HIGH;HAL_GPIO_Init(GPIOB,&g);select(false);
    g.Pin=GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5;g.Mode=GPIO_MODE_AF_PP;g.Alternate=GPIO_AF6_SPI3;HAL_GPIO_Init(GPIOB,&g);
    SPI3->CR1=SPI_CR1_MSTR|SPI_CR1_SSM|SPI_CR1_SSI|SPI_CR1_BR_0|SPI_CR1_SPE;SPI3->CR2=0;
    uint8_t a=0,b=0,c=0;select(true);bool ok=byte(0x9f)&&transfer(255,&a)&&transfer(255,&b)&&transfer(255,&c);select(false);
    jedec=((uint32_t)a<<16)|((uint32_t)b<<8)|c;
    if(!ok||a!=0xef||(b!=0x40&&b!=0x60)||c<0x15||c>0x18){capacity=0;writes=false;return false;}
    capacity=1UL<<c;base=capacity-1048576U;writes=permit;return true;
}
uint32_t rg_w25q_jedec(void){return jedec;}
uint32_t rg_w25q_capacity(void){return capacity;}
bool rg_w25q_read(void*p,uint32_t at,void*out,uint32_t n){
    (void)p;if(!capacity||at>capacity||n>capacity-at)return false;
    uint8_t *b=out;select(true);bool ok=addressed(3,at);for(uint32_t i=0;ok&&i<n;i++)ok=transfer(255,b+i);select(false);return ok;
}
static bool program(void*p,uint32_t at,const void*data,uint32_t n){
    (void)p;if(!writes||at<base||at>capacity||n>capacity-at||!n||n>256||((at&255U)+n)>256)return false;
    if(!write_enable())return false;
    select(true);bool ok=addressed(2,at);const uint8_t*b=data;for(uint32_t i=0;ok&&i<n;i++)ok=byte(b[i]);select(false);return ok;
}
static bool erase(void*p,uint32_t at){(void)p;if(!writes||at<base||at>=capacity||at%4096)return false;if(!write_enable())return false;select(true);bool ok=addressed(0x20,at);select(false);return ok;}
static int busy(void*p){(void)p;uint8_t out=0;select(true);bool ok=byte(5)&&transfer(255,&out);select(false);return ok?(out&1):-1;}
rg_journal_io_t rg_w25q_io(void){rg_journal_io_t io={0,rg_w25q_read,program,erase,busy,0};return io;}
