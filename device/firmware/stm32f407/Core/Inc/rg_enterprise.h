#ifndef RG_ENTERPRISE_H
#define RG_ENTERPRISE_H
#include <stdint.h>
typedef struct {
    uint32_t magic,version,heartbeat,imu_reads,imu_errors,imu_late,calibrated;
    uint32_t brake_events,speed_events,road_events,camera_ok,camera_errors,photo_dropped;
    uint32_t flash_jedec,flash_capacity,storage_writable,storage_fault,storage_full,storage_queued;
    uint32_t modem_ready,modem_errors,event_acks,photo_acks,upload_errors,policy_version;
    uint32_t voice_sent,voice_errors,identity_fault,gps_valid,restore_done;
    uint32_t volatile_photos,camera_ready,camera_pid,gps_bytes,gps_errors;
    uint32_t read_address,read_length,read_command,read_result;
    uint8_t read_buffer[256]; /* SWD read-only backup mailbox, never accepts write commands. */
} RgEnterpriseDiagnostics;
extern volatile RgEnterpriseDiagnostics enterprise_diag;
#endif
