#ifndef BRINGUP_V12_H
#define BRINGUP_V12_H
#include "main.h"

/* Fixed-width diagnostic counters remain readable through SWD without USB. */
typedef struct {
    uint32_t magic, version, stage, heartbeat_ms;
    uint32_t imu_ready, imu_id, imu_reads, imu_errors;
    uint32_t gps_bytes, gps_errors, gps_lines, gps_rmc, gps_valid;
    uint32_t gps_last_ms, gps_speed_milli_kph;
    int32_t latitude_e7, longitude_e7;
    uint32_t camera_ready, camera_pid, camera_attempts, camera_ok;
    uint32_t camera_errors, camera_bytes, camera_last_status;
    uint32_t modem_bytes, modem_errors, modem_ok, modem_error_lines;
    uint32_t modem_queries_sent, modem_timeouts, modem_sim_ready;
    int32_t modem_csq;
    uint32_t mp3_bytes, mp3_errors, mp3_valid_frames, mp3_bad_frames;
    uint32_t mp3_queries_sent, mp3_last_command, mp3_last_value;
    uint32_t gps_overruns, modem_overruns, mp3_overruns;
    uint32_t usb_image_ok, usb_image_errors;
    char gps_last_line[128], modem_last_line[160];
} BringupDiagnostics;

extern volatile BringupDiagnostics bringup_diag;
void Bringup_RxComplete(UART_HandleTypeDef *uart);
void Bringup_RxError(UART_HandleTypeDef *uart);
#endif
