#include "ov2640.h"
#include <string.h>

#define OV2640_ADDR                    (0x30U << 1)
#define OV2640_JPEG_BUFFER_WORDS       (OV2640_JPEG_BUFFER_SIZE_BYTES / 4U)
#define OV2640_CAPTURE_WAIT_MS         1000U
#define OV2640_VSYNC_TIMEOUT_MS        1000U

/* Verified hardware wiring: VSYNC is PB7 and active-low on this module. */
#define OV2640_VSYNC_GPIO_Port         GPIOB
#define OV2640_VSYNC_Pin               GPIO_PIN_7

typedef struct
{
    uint8_t reg;
    uint8_t val;
} OV2640_RegVal;

/* DMA-accessible SRAM buffer. STM32F407 has no data cache. */
__attribute__((aligned(4))) static uint32_t s_jpeg_buffer[OV2640_JPEG_BUFFER_WORDS];

static HAL_StatusTypeDef OV2640_WriteReg(OV2640_Handle_t *camera,
                                         uint8_t reg,
                                         uint8_t data);
static HAL_StatusTypeDef OV2640_ReadReg(OV2640_Handle_t *camera,
                                        uint8_t reg,
                                        uint8_t *data);
static void OV2640_HardwareReset(void);
static HAL_StatusTypeDef OV2640_WriteTable(OV2640_Handle_t *camera,
                                           const OV2640_RegVal *table);
static HAL_StatusTypeDef OV2640_SoftwareReset(OV2640_Handle_t *camera);
static HAL_StatusTypeDef OV2640_InitJPEG_QVGA(OV2640_Handle_t *camera);
static HAL_StatusTypeDef OV2640_EnableAutoAECAGC(OV2640_Handle_t *camera);
static HAL_StatusTypeDef OV2640_WaitAutoAdjust(uint8_t frame_count);
static uint8_t OV2640_FindJPEG(const uint8_t *data,
                               uint32_t data_len,
                               uint32_t *jpeg_offset,
                               uint32_t *jpeg_length);

/*
 * Register tables are retained from the already verified project version.
 * Original table source attribution is preserved from the previous main.c:
 * SimpleMethod/STM32-OV2640, Src/ov2640.c (MIT License).
 */

static const OV2640_RegVal ov2640_jpeg_init_table[] =
{
  {0xFF,0x00},{0x2C,0xFF},{0x2E,0xDF},{0xFF,0x01},{0x3C,0x32},
  {0x11,0x00},{0x09,0x02},{0x04,0x28},{0x13,0xE5},{0x14,0x48},
  {0x2C,0x0C},{0x33,0x78},{0x3A,0x33},{0x3B,0xFB},{0x3E,0x00},
  {0x43,0x11},{0x16,0x10},{0x39,0x92},{0x35,0xDA},{0x22,0x1A},
  {0x37,0xC3},{0x23,0x00},{0x34,0xC0},{0x36,0x1A},{0x06,0x88},
  {0x07,0xC0},{0x0D,0x87},{0x0E,0x41},{0x4C,0x00},{0x48,0x00},
  {0x5B,0x00},{0x42,0x03},{0x4A,0x81},{0x21,0x99},{0x24,0x40},
  {0x25,0x38},{0x26,0x82},{0x5C,0x00},{0x63,0x00},{0x61,0x70},
  {0x62,0x80},{0x7C,0x05},{0x20,0x80},{0x28,0x30},{0x6C,0x00},
  {0x6D,0x80},{0x6E,0x00},{0x70,0x02},{0x71,0x94},{0x73,0xC1},
  {0x12,0x40},{0x17,0x11},{0x18,0x43},{0x19,0x00},{0x1A,0x4B},
  {0x32,0x09},{0x37,0xC0},{0x4F,0x60},{0x50,0xA8},{0x6D,0x00},
  {0x3D,0x38},{0x46,0x3F},{0x4F,0x60},{0x0C,0x3C},{0xFF,0x00},
  {0xE5,0x7F},{0xF9,0xC0},{0x41,0x24},{0xE0,0x14},{0x76,0xFF},
  {0x33,0xA0},{0x42,0x20},{0x43,0x18},{0x4C,0x00},{0x87,0xD5},
  {0x88,0x3F},{0xD7,0x03},{0xD9,0x10},{0xD3,0x82},{0xC8,0x08},
  {0xC9,0x80},{0x7C,0x00},{0x7D,0x00},{0x7C,0x03},{0x7D,0x48},
  {0x7D,0x48},{0x7C,0x08},{0x7D,0x20},{0x7D,0x10},{0x7D,0x0E},
  {0x90,0x00},{0x91,0x0E},{0x91,0x1A},{0x91,0x31},{0x91,0x5A},
  {0x91,0x69},{0x91,0x75},{0x91,0x7E},{0x91,0x88},{0x91,0x8F},
  {0x91,0x96},{0x91,0xA3},{0x91,0xAF},{0x91,0xC4},{0x91,0xD7},
  {0x91,0xE8},{0x91,0x20},{0x92,0x00},{0x93,0x06},{0x93,0xE3},
  {0x93,0x05},{0x93,0x05},{0x93,0x00},{0x93,0x04},{0x93,0x00},
  {0x93,0x00},{0x93,0x00},{0x93,0x00},{0x93,0x00},{0x93,0x00},
  {0x93,0x00},{0x96,0x00},{0x97,0x08},{0x97,0x19},{0x97,0x02},
  {0x97,0x0C},{0x97,0x24},{0x97,0x30},{0x97,0x28},{0x97,0x26},
  {0x97,0x02},{0x97,0x98},{0x97,0x80},{0x97,0x00},{0x97,0x00},
  {0xC3,0xED},{0xA4,0x00},{0xA8,0x00},{0xC5,0x11},{0xC6,0x51},
  {0xBF,0x80},{0xC7,0x10},{0xB6,0x66},{0xB8,0xA5},{0xB7,0x64},
  {0xB9,0x7C},{0xB3,0xAF},{0xB4,0x97},{0xB5,0xFF},{0xB0,0xC5},
  {0xB1,0x94},{0xB2,0x0F},{0xC4,0x5C},{0xC0,0x64},{0xC1,0x4B},
  {0x8C,0x00},{0x86,0x3D},{0x50,0x00},{0x51,0xC8},{0x52,0x96},
  {0x53,0x00},{0x54,0x00},{0x55,0x00},{0x5A,0xC8},{0x5B,0x96},
  {0x5C,0x00},{0xD3,0x00},{0xC3,0xED},{0x7F,0x00},{0xDA,0x00},
  {0xE5,0x1F},{0xE1,0x67},{0xE0,0x00},{0xDD,0x7F},{0x05,0x00},
  {0x12,0x40},{0xD3,0x04},{0xC0,0x16},{0xC1,0x12},{0x8C,0x00},
  {0x86,0x3D},{0x50,0x00},{0x51,0x2C},{0x52,0x24},{0x53,0x00},
  {0x54,0x00},{0x55,0x00},{0x5A,0x2C},{0x5B,0x24},{0x5C,0x00},
  {0xFF,0xFF}
};

static const OV2640_RegVal ov2640_yuv422_table[] =
{
  {0xFF,0x00},{0x05,0x00},{0xDA,0x10},{0xD7,0x03},{0xDF,0x00},
  {0x33,0x80},{0x3C,0x40},{0xE1,0x77},{0x00,0x00},{0xFF,0xFF}
};

static const OV2640_RegVal ov2640_jpeg_table[] =
{
  {0xE0,0x14},{0xE1,0x77},{0xE5,0x1F},{0xD7,0x03},{0xDA,0x10},
  {0xE0,0x00},{0xFF,0x01},{0x04,0x08},{0xFF,0xFF}
};

static const OV2640_RegVal ov2640_qvga_320x240_table[] =
{
  {0xFF,0x01},{0x12,0x40},{0x17,0x11},{0x18,0x43},{0x19,0x00},
  {0x1A,0x4B},{0x32,0x09},{0x4F,0xCA},{0x50,0xA8},{0x5A,0x23},
  {0x6D,0x00},{0x39,0x12},{0x35,0xDA},{0x22,0x1A},{0x37,0xC3},
  {0x23,0x00},{0x34,0xC0},{0x36,0x1A},{0x06,0x88},{0x07,0xC0},
  {0x0D,0x87},{0x0E,0x41},{0x4C,0x00},{0xFF,0x00},{0xE0,0x04},
  {0xC0,0x64},{0xC1,0x4B},{0x86,0x35},{0x50,0x89},{0x51,0xC8},
  {0x52,0x96},{0x53,0x00},{0x54,0x00},{0x55,0x00},{0x57,0x00},
  {0x5A,0x50},{0x5B,0x3C},{0x5C,0x00},{0xE0,0x00},{0xFF,0xFF}
};


static HAL_StatusTypeDef OV2640_WriteReg(OV2640_Handle_t *camera,
                                         uint8_t reg,
                                         uint8_t data)
{
    uint8_t buf[2] = {reg, data};

    if ((camera == NULL) || (camera->hi2c == NULL))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Master_Transmit(camera->hi2c,
                                   OV2640_ADDR,
                                   buf,
                                   2U,
                                   100U);
}

static HAL_StatusTypeDef OV2640_ReadReg(OV2640_Handle_t *camera,
                                        uint8_t reg,
                                        uint8_t *data)
{
    HAL_StatusTypeDef status;

    if ((camera == NULL) || (camera->hi2c == NULL) || (data == NULL))
    {
        return HAL_ERROR;
    }

    status = HAL_I2C_Master_Transmit(camera->hi2c,
                                     OV2640_ADDR,
                                     &reg,
                                     1U,
                                     100U);
    if (status != HAL_OK)
    {
        return status;
    }

    return HAL_I2C_Master_Receive(camera->hi2c,
                                  OV2640_ADDR,
                                  data,
                                  1U,
                                  100U);
}

static void OV2640_HardwareReset(void)
{
    HAL_GPIO_WritePin(CAM_PWDN_GPIO_Port, CAM_PWDN_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);

    HAL_GPIO_WritePin(CAM_RST_GPIO_Port, CAM_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(CAM_RST_GPIO_Port, CAM_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
}

HAL_StatusTypeDef OV2640_ReadID(OV2640_Handle_t *camera)
{
    HAL_StatusTypeDef status;

    if (camera == NULL)
    {
        return HAL_ERROR;
    }

    camera->midh = 0U;
    camera->midl = 0U;
    camera->pidh = 0U;
    camera->pidl = 0U;

    status = OV2640_WriteReg(camera, 0xFFU, 0x01U);
    if (status != HAL_OK) return status;

    HAL_Delay(10);

    status = OV2640_ReadReg(camera, 0x1CU, &camera->midh);
    if (status != HAL_OK) return status;
    status = OV2640_ReadReg(camera, 0x1DU, &camera->midl);
    if (status != HAL_OK) return status;
    status = OV2640_ReadReg(camera, 0x0AU, &camera->pidh);
    if (status != HAL_OK) return status;
    status = OV2640_ReadReg(camera, 0x0BU, &camera->pidl);

    return status;
}

static HAL_StatusTypeDef OV2640_WriteTable(OV2640_Handle_t *camera,
                                           const OV2640_RegVal *table)
{
    uint16_t i = 0U;
    HAL_StatusTypeDef status;

    if ((camera == NULL) || (table == NULL))
    {
        return HAL_ERROR;
    }

    while (!((table[i].reg == 0xFFU) && (table[i].val == 0xFFU)))
    {
        status = OV2640_WriteReg(camera, table[i].reg, table[i].val);
        if (status != HAL_OK)
        {
            camera->table_fail_index = i;
            camera->table_fail_reg = table[i].reg;
            camera->table_fail_val = table[i].val;
            return status;
        }

        HAL_Delay(1);
        i++;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef OV2640_SoftwareReset(OV2640_Handle_t *camera)
{
    HAL_StatusTypeDef status;

    status = OV2640_WriteReg(camera, 0xFFU, 0x01U);
    if (status != HAL_OK) return status;

    status = OV2640_WriteReg(camera, 0x12U, 0x80U);
    if (status != HAL_OK) return status;

    HAL_Delay(100);
    return HAL_OK;
}

static HAL_StatusTypeDef OV2640_InitJPEG_QVGA(OV2640_Handle_t *camera)
{
    HAL_StatusTypeDef status;

    camera->table_fail_index = 0U;
    camera->table_fail_reg = 0U;
    camera->table_fail_val = 0U;

    status = OV2640_SoftwareReset(camera);
    if (status != HAL_OK) return status;

    status = OV2640_WriteTable(camera, ov2640_jpeg_init_table);
    if (status != HAL_OK) return status;

    status = OV2640_WriteTable(camera, ov2640_yuv422_table);
    if (status != HAL_OK) return status;

    status = OV2640_WriteTable(camera, ov2640_jpeg_table);
    if (status != HAL_OK) return status;

    HAL_Delay(10);

    status = OV2640_WriteReg(camera, 0xFFU, 0x01U);
    if (status != HAL_OK) return status;

    status = OV2640_WriteReg(camera, 0x15U, 0x00U);
    if (status != HAL_OK) return status;

    HAL_Delay(10);

    status = OV2640_WriteTable(camera, ov2640_qvga_320x240_table);
    if (status != HAL_OK) return status;

    HAL_Delay(200);
    return HAL_OK;
}

static HAL_StatusTypeDef OV2640_EnableAutoAECAGC(OV2640_Handle_t *camera)
{
    HAL_StatusTypeDef status;
    uint8_t com8 = 0U;

    status = OV2640_WriteReg(camera, 0xFFU, 0x01U);
    if (status != HAL_OK) return status;

    /* Normal image path, color bar OFF. */
    status = OV2640_WriteReg(camera, 0x12U, 0x40U);
    if (status != HAL_OK) return status;

    status = OV2640_ReadReg(camera, 0x13U, &com8);
    if (status != HAL_OK) return status;

    /* COM8 bit2 = AGC, bit0 = AEC. */
    com8 |= 0x05U;
    status = OV2640_WriteReg(camera, 0x13U, com8);
    if (status != HAL_OK) return status;

    HAL_Delay(20);
    return HAL_OK;
}

HAL_StatusTypeDef OV2640_ReadExposure(OV2640_Handle_t *camera,
                                      OV2640_Exposure_t *exposure)
{
    HAL_StatusTypeDef status;

    if ((camera == NULL) || (exposure == NULL))
    {
        return HAL_ERROR;
    }

    status = OV2640_WriteReg(camera, 0xFFU, 0x01U);
    if (status != HAL_OK) return status;

    status = OV2640_ReadReg(camera, 0x13U, &exposure->com8);
    if (status != HAL_OK) return status;
    status = OV2640_ReadReg(camera, 0x14U, &exposure->com9);
    if (status != HAL_OK) return status;
    status = OV2640_ReadReg(camera, 0x00U, &exposure->gain);
    if (status != HAL_OK) return status;
    status = OV2640_ReadReg(camera, 0x10U, &exposure->aec);

    return status;
}

static HAL_StatusTypeDef OV2640_WaitAutoAdjust(uint8_t frame_count)
{
    uint8_t i;
    uint32_t start;

    for (i = 0U; i < frame_count; i++)
    {
        start = HAL_GetTick();
        while (HAL_GPIO_ReadPin(OV2640_VSYNC_GPIO_Port, OV2640_VSYNC_Pin) == GPIO_PIN_SET)
        {
            if ((HAL_GetTick() - start) > OV2640_VSYNC_TIMEOUT_MS)
            {
                return HAL_TIMEOUT;
            }
        }

        start = HAL_GetTick();
        while (HAL_GPIO_ReadPin(OV2640_VSYNC_GPIO_Port, OV2640_VSYNC_Pin) == GPIO_PIN_RESET)
        {
            if ((HAL_GetTick() - start) > OV2640_VSYNC_TIMEOUT_MS)
            {
                return HAL_TIMEOUT;
            }
        }
    }

    return HAL_OK;
}

static uint8_t OV2640_FindJPEG(const uint8_t *data,
                               uint32_t data_len,
                               uint32_t *jpeg_offset,
                               uint32_t *jpeg_length)
{
    uint32_t soi = data_len;
    uint32_t i;

    if ((data == NULL) || (jpeg_offset == NULL) ||
        (jpeg_length == NULL) || (data_len < 4U))
    {
        return 0U;
    }

    for (i = 0U; i + 1U < data_len; i++)
    {
        if ((data[i] == 0xFFU) && (data[i + 1U] == 0xD8U))
        {
            soi = i;
            break;
        }
    }

    if (soi == data_len)
    {
        return 0U;
    }

    for (i = soi + 2U; i + 1U < data_len; i++)
    {
        if ((data[i] == 0xFFU) && (data[i + 1U] == 0xD9U))
        {
            *jpeg_offset = soi;
            *jpeg_length = (i + 2U) - soi;
            return 1U;
        }
    }

    return 0U;
}

HAL_StatusTypeDef OV2640_Begin(OV2640_Handle_t *camera,
                               I2C_HandleTypeDef *hi2c,
                               DCMI_HandleTypeDef *hdcmi,
                               DMA_HandleTypeDef *hdma_dcmi)
{
    HAL_StatusTypeDef status;

    if ((camera == NULL) || (hi2c == NULL) ||
        (hdcmi == NULL) || (hdma_dcmi == NULL))
    {
        return HAL_ERROR;
    }

    memset(camera, 0, sizeof(*camera));
    camera->hi2c = hi2c;
    camera->hdcmi = hdcmi;
    camera->hdma_dcmi = hdma_dcmi;

    OV2640_HardwareReset();

    /* Preserve the old, proven recovery step after camera reset. */
    (void)HAL_I2C_DeInit(camera->hi2c);
    HAL_Delay(20);
    status = HAL_I2C_Init(camera->hi2c);
    if (status != HAL_OK) return status;
    HAL_Delay(20);

    status = OV2640_ReadID(camera);
    if (status != HAL_OK) return status;

    if ((camera->pidh != 0x26U) || (camera->pidl != 0x42U))
    {
        return HAL_ERROR;
    }

    status = OV2640_InitJPEG_QVGA(camera);
    if (status != HAL_OK) return status;

    status = OV2640_EnableAutoAECAGC(camera);
    if (status != HAL_OK) return status;

    /* Preserve the previous proven behavior: a warm-up timeout is diagnostic,
     * but it does not disable capture. */
    camera->warmup_status = OV2640_WaitAutoAdjust(OV2640_WARMUP_FRAMES);

    camera->ready = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef OV2640_CaptureJPEG(OV2640_Handle_t *camera,
                                     OV2640_JPEGFrame_t *frame)
{
    HAL_StatusTypeDef status;
    uint32_t remaining_words;
    uint32_t used_words;
    uint32_t jpeg_offset = 0U;
    uint32_t jpeg_length = 0U;

    if ((camera == NULL) || (frame == NULL) ||
        (camera->ready == 0U) || (camera->hdcmi == NULL) ||
        (camera->hdma_dcmi == NULL))
    {
        return HAL_ERROR;
    }

    memset(frame, 0, sizeof(*frame));
    memset(s_jpeg_buffer, 0, sizeof(s_jpeg_buffer));

    camera->hdcmi->ErrorCode = HAL_DCMI_ERROR_NONE;
    __HAL_DCMI_DISABLE_IT(camera->hdcmi, DCMI_IT_LINE | DCMI_IT_VSYNC);

    status = HAL_DCMI_Start_DMA(camera->hdcmi,
                                DCMI_MODE_SNAPSHOT,
                                (uint32_t)s_jpeg_buffer,
                                OV2640_JPEG_BUFFER_WORDS);
    if (status != HAL_OK)
    {
        frame->dcmi_error = camera->hdcmi->ErrorCode;
        return status;
    }

    HAL_Delay(OV2640_CAPTURE_WAIT_MS);

    remaining_words = __HAL_DMA_GET_COUNTER(camera->hdma_dcmi);
    if (remaining_words > OV2640_JPEG_BUFFER_WORDS)
    {
        remaining_words = OV2640_JPEG_BUFFER_WORDS;
    }

    used_words = OV2640_JPEG_BUFFER_WORDS - remaining_words;
    frame->captured_bytes = used_words * 4U;

    (void)HAL_DCMI_Stop(camera->hdcmi);
    frame->dcmi_error = camera->hdcmi->ErrorCode;

    if (frame->captured_bytes == 0U)
    {
        return HAL_ERROR;
    }

    if (!OV2640_FindJPEG((const uint8_t *)s_jpeg_buffer,
                         frame->captured_bytes,
                         &jpeg_offset,
                         &jpeg_length))
    {
        return HAL_ERROR;
    }

    frame->data = &((const uint8_t *)s_jpeg_buffer)[jpeg_offset];
    frame->length = jpeg_length;
    return HAL_OK;
}

uint8_t OV2640_IsReady(const OV2640_Handle_t *camera)
{
    return ((camera != NULL) && (camera->ready != 0U)) ? 1U : 0U;
}
