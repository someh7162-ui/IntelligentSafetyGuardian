#ifndef __USB_STREAM_H
#define __USB_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

void USB_Stream_Print(const char *str);
HAL_StatusTypeDef USB_Stream_SendBytes(const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* __USB_STREAM_H */
