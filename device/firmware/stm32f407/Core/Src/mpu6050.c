#include "mpu6050.h"

/* MPU6050 registers */
#define MPU6050_REG_SMPLRT_DIV      0x19U
#define MPU6050_REG_CONFIG          0x1AU
#define MPU6050_REG_GYRO_CONFIG     0x1BU
#define MPU6050_REG_ACCEL_CONFIG    0x1CU
#define MPU6050_REG_ACCEL_XOUT_H    0x3BU
#define MPU6050_REG_PWR_MGMT_1      0x6BU
#define MPU6050_REG_WHO_AM_I        0x75U

#define MPU6050_TIMEOUT_MS          100U

/* Scale factors for +/-2 g and +/-250 deg/s */
#define MPU6050_ACCEL_LSB_PER_G     16384.0f
#define MPU6050_GYRO_LSB_PER_DPS    131.0f

static I2C_HandleTypeDef *s_mpu_i2c = NULL;
static uint8_t s_mpu_ready = 0U;

static HAL_StatusTypeDef MPU6050_WriteReg(uint8_t reg, uint8_t value)
{
    if (s_mpu_i2c == NULL)
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(s_mpu_i2c,
                             MPU6050_ADDR,
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             &value,
                             1U,
                             MPU6050_TIMEOUT_MS);
}

static HAL_StatusTypeDef MPU6050_ReadRegs(uint8_t reg,
                                         uint8_t *data,
                                         uint16_t length)
{
    if ((s_mpu_i2c == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(s_mpu_i2c,
                            MPU6050_ADDR,
                            reg,
                            I2C_MEMADD_SIZE_8BIT,
                            data,
                            length,
                            MPU6050_TIMEOUT_MS);
}

HAL_StatusTypeDef MPU6050_ReadWhoAmI(uint8_t *who_am_i)
{
    if (who_am_i == NULL)
    {
        return HAL_ERROR;
    }

    return MPU6050_ReadRegs(MPU6050_REG_WHO_AM_I, who_am_i, 1U);
}

HAL_StatusTypeDef MPU6050_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef status;
    uint8_t who_am_i = 0U;

    if (hi2c == NULL)
    {
        return HAL_ERROR;
    }

    s_mpu_i2c = hi2c;
    s_mpu_ready = 0U;

    /* Make sure the device acknowledges on the bus first. */
    status = HAL_I2C_IsDeviceReady(s_mpu_i2c,
                                   MPU6050_ADDR,
                                   3U,
                                   MPU6050_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return status;
    }

    status = MPU6050_ReadWhoAmI(&who_am_i);
    if (status != HAL_OK)
    {
        return status;
    }

    /* AD0 is currently wired low, therefore WHO_AM_I should be 0x68. */
    if (who_am_i != MPU6050_ADDR_7BIT)
    {
        return HAL_ERROR;
    }

    /* Wake sensor; select internal clock first for simple bring-up. */
    status = MPU6050_WriteReg(MPU6050_REG_PWR_MGMT_1, 0x00U);
    if (status != HAL_OK)
    {
        return status;
    }
    HAL_Delay(100);

    /* Sample rate divider. With 1 kHz base rate: 1000 / (1 + 9) = 100 Hz. */
    status = MPU6050_WriteReg(MPU6050_REG_SMPLRT_DIV, 0x09U);
    if (status != HAL_OK)
    {
        return status;
    }

    /* DLPF_CFG = 3: useful starting point for motion sensing. */
    status = MPU6050_WriteReg(MPU6050_REG_CONFIG, 0x03U);
    if (status != HAL_OK)
    {
        return status;
    }

    /* FS_SEL = 0 -> +/-250 deg/s. */
    status = MPU6050_WriteReg(MPU6050_REG_GYRO_CONFIG, 0x00U);
    if (status != HAL_OK)
    {
        return status;
    }

    /* AFS_SEL = 0 -> +/-2 g. */
    status = MPU6050_WriteReg(MPU6050_REG_ACCEL_CONFIG, 0x00U);
    if (status != HAL_OK)
    {
        return status;
    }

    s_mpu_ready = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef MPU6050_Read(MPU6050_Data_t *data)
{
    HAL_StatusTypeDef status;
    uint8_t raw[14];

    if ((data == NULL) || (s_mpu_ready == 0U))
    {
        return HAL_ERROR;
    }

    status = MPU6050_ReadRegs(MPU6050_REG_ACCEL_XOUT_H, raw, sizeof(raw));
    if (status != HAL_OK)
    {
        return status;
    }

    data->accel_x_raw = (int16_t)(((uint16_t)raw[0]  << 8) | raw[1]);
    data->accel_y_raw = (int16_t)(((uint16_t)raw[2]  << 8) | raw[3]);
    data->accel_z_raw = (int16_t)(((uint16_t)raw[4]  << 8) | raw[5]);

    data->temperature_raw = (int16_t)(((uint16_t)raw[6] << 8) | raw[7]);

    data->gyro_x_raw = (int16_t)(((uint16_t)raw[8]  << 8) | raw[9]);
    data->gyro_y_raw = (int16_t)(((uint16_t)raw[10] << 8) | raw[11]);
    data->gyro_z_raw = (int16_t)(((uint16_t)raw[12] << 8) | raw[13]);

    data->accel_x_g = (float)data->accel_x_raw / MPU6050_ACCEL_LSB_PER_G;
    data->accel_y_g = (float)data->accel_y_raw / MPU6050_ACCEL_LSB_PER_G;
    data->accel_z_g = (float)data->accel_z_raw / MPU6050_ACCEL_LSB_PER_G;

    data->gyro_x_dps = (float)data->gyro_x_raw / MPU6050_GYRO_LSB_PER_DPS;
    data->gyro_y_dps = (float)data->gyro_y_raw / MPU6050_GYRO_LSB_PER_DPS;
    data->gyro_z_dps = (float)data->gyro_z_raw / MPU6050_GYRO_LSB_PER_DPS;

    data->temperature_c = ((float)data->temperature_raw / 340.0f) + 36.53f;

    return HAL_OK;
}

uint8_t MPU6050_IsReady(void)
{
    return s_mpu_ready;
}
