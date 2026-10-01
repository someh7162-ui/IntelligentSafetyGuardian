#ifndef __OV2640_H
#define __OV2640_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define OV2640_JPEG_BUFFER_SIZE_BYTES  (64U * 1024U)
#define OV2640_WARMUP_FRAMES           10U

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    DCMI_HandleTypeDef *hdcmi;
    DMA_HandleTypeDef *hdma_dcmi;

    uint8_t midh;
    uint8_t midl;
    uint8_t pidh;
    uint8_t pidl;

    uint16_t table_fail_index;
    uint8_t table_fail_reg;
    uint8_t table_fail_val;

    HAL_StatusTypeDef warmup_status;
    uint8_t ready;
} OV2640_Handle_t;

typedef struct
{
    uint8_t com8;
    uint8_t com9;
    uint8_t gain;
    uint8_t aec;
} OV2640_Exposure_t;

typedef struct
{
    const uint8_t *data;
    uint32_t length;
    uint32_t captured_bytes;
    uint32_t dcmi_error;
} OV2640_JPEGFrame_t;

/*
 * Attach the CubeMX-generated I2C2/DCMI/DMA handles, reset the sensor,
 * verify PID=0x2642, configure JPEG QVGA, enable AEC/AGC, and warm up.
 */
HAL_StatusTypeDef OV2640_Begin(OV2640_Handle_t *camera,
                               I2C_HandleTypeDef *hi2c,
                               DCMI_HandleTypeDef *hdcmi,
                               DMA_HandleTypeDef *hdma_dcmi);

HAL_StatusTypeDef OV2640_ReadID(OV2640_Handle_t *camera);
HAL_StatusTypeDef OV2640_ReadExposure(OV2640_Handle_t *camera,
                                      OV2640_Exposure_t *exposure);
HAL_StatusTypeDef OV2640_CaptureJPEG(OV2640_Handle_t *camera,
                                     OV2640_JPEGFrame_t *frame);

uint8_t OV2640_IsReady(const OV2640_Handle_t *camera);

#ifdef __cplusplus
}
#endif

#endif /* __OV2640_H */
