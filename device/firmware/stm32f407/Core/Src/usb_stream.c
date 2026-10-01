#include "usb_stream.h"
#include "usbd_cdc_if.h"
#include <string.h>

#define USB_STREAM_CHUNK_SIZE 256U

void USB_Stream_Print(const char *str)
{
    uint32_t start;
    uint16_t len;

    if (str == NULL)
    {
        return;
    }

    start = HAL_GetTick();
    len = (uint16_t)strlen(str);

    while (CDC_Transmit_FS((uint8_t *)str, len) == USBD_BUSY)
    {
        if ((HAL_GetTick() - start) > 100U)
        {
            return;
        }
        HAL_Delay(1);
    }

    HAL_Delay(2);
}

HAL_StatusTypeDef USB_Stream_SendBytes(const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;

    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    while (offset < length)
    {
        uint16_t chunk = (uint16_t)(((length - offset) > USB_STREAM_CHUNK_SIZE)
                                  ? USB_STREAM_CHUNK_SIZE
                                  : (length - offset));
        uint32_t start = HAL_GetTick();

        while (CDC_Transmit_FS((uint8_t *)&data[offset], chunk) == USBD_BUSY)
        {
            if ((HAL_GetTick() - start) > 500U)
            {
                return HAL_TIMEOUT;
            }
            HAL_Delay(1);
        }

        offset += chunk;
        HAL_Delay(1);
    }

    return HAL_OK;
}
