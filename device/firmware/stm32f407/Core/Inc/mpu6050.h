#ifndef __MPU6050_H
#define __MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define MPU6050_ADDR_7BIT          0x68U
#define MPU6050_ADDR               (MPU6050_ADDR_7BIT << 1)

typedef struct
{
    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;

    int16_t gyro_x_raw;
    int16_t gyro_y_raw;
    int16_t gyro_z_raw;

    int16_t temperature_raw;

    float accel_x_g;
    float accel_y_g;
    float accel_z_g;

    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;

    float temperature_c;
} MPU6050_Data_t;

/* Initialize MPU6050 on the supplied I2C bus.
 * Current defaults:
 *   Accelerometer: +/-2 g
 *   Gyroscope:     +/-250 deg/s
 *   DLPF:          enabled
 */
HAL_StatusTypeDef MPU6050_Init(I2C_HandleTypeDef *hi2c);

/* Read WHO_AM_I. Expected value is 0x68 when AD0 is low. */
HAL_StatusTypeDef MPU6050_ReadWhoAmI(uint8_t *who_am_i);

/* Read accelerometer, temperature and gyroscope in one 14-byte transaction. */
HAL_StatusTypeDef MPU6050_Read(MPU6050_Data_t *data);

/* Returns 1 after a successful MPU6050_Init(), otherwise 0. */
uint8_t MPU6050_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H */
