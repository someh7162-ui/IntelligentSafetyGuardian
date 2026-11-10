#include "usb_stream.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include <string.h>
extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t staging[256];
uint8_t USB_Stream_Connected(void) {
    return hUsbDeviceFS.dev_state==USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData!=NULL;
}
static HAL_StatusTypeDef wait_idle(void) {
    uint32_t start=HAL_GetTick();
    while(USB_Stream_Connected()) {
        USBD_CDC_HandleTypeDef *cdc=(USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
        if(cdc->TxState==0) return HAL_OK;
        if((uint32_t)(HAL_GetTick()-start)>=100u) return HAL_TIMEOUT;
        HAL_Delay(1);
    }
    return HAL_ERROR;
}
HAL_StatusTypeDef USB_Stream_SendBytes(const uint8_t *data,uint32_t length) {
    if(data==NULL || length==0 || !USB_Stream_Connected()) return HAL_ERROR;
    while(length) {
        HAL_StatusTypeDef status=wait_idle();
        if(status!=HAL_OK) return status;
        uint16_t count=(uint16_t)(length>sizeof staging?sizeof staging:length);
        memcpy(staging,data,count);
        if(CDC_Transmit_FS(staging,count)!=USBD_OK) return HAL_ERROR;
        status=wait_idle();
        if(status!=HAL_OK) return status;
        data+=count; length-=count;
    }
    return HAL_OK;
}
void USB_Stream_Print(const char *text) {
    if(text && USB_Stream_Connected()) (void)USB_Stream_SendBytes((const uint8_t *)text,(uint32_t)strlen(text));
}
