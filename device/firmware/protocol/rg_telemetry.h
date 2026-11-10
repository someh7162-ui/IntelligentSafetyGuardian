#ifndef RG_TELEMETRY_H
#define RG_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>
#include "rg_gps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RG_TELEMETRY_DEVICE_ID_MIN_LENGTH 3U
#define RG_TELEMETRY_DEVICE_ID_MAX_LENGTH 64U
#define RG_TELEMETRY_SAMPLE_ID_MAX_LENGTH 80U
#define RG_TELEMETRY_DEFAULT_MAX_GPS_AGE_MS 5000U

typedef enum
{
    RG_TELEMETRY_OK = 0,
    RG_TELEMETRY_INVALID_ARGUMENT,
    RG_TELEMETRY_INVALID_ID,
    RG_TELEMETRY_INVALID_TIMESTAMP,
    RG_TELEMETRY_INVALID_FIX,
    RG_TELEMETRY_BUFFER_TOO_SMALL
} rg_telemetry_status_t;

typedef struct
{
    const char *device_id;
    const char *sample_id;
    /* Trusted Unix milliseconds supplied by the caller; never a HAL tick. */
    int64_t captured_at_ms;
    const rg_gps_fix_t *gps;
    /* Same monotonic uint32_t clock used for gps->receipt_tick_ms. */
    uint32_t now_tick_ms;
    uint32_t max_gps_age_ms;
} rg_telemetry_input_t;

/*
 * Build the complete RiderGuard telemetry JSON, without heap allocation,
 * floating point, printf, a device token, or a HAL dependency.
 *
 * All input pointers and out must be non-NULL except written, which is optional.
 * IDs must be NUL-terminated ASCII [A-Za-z0-9_-], device length 3..64 and
 * sample length 1..80. Output must not alias input objects or strings.
 * captured_at_ms must be positive. Its actual trust, server time window, and
 * stability across retries are the caller's responsibility; gps->unix_ms and
 * gps->utc_valid never substitute for the supplied timestamp.
 *
 * GPS is fresh when (uint32_t)(now_tick_ms - receipt_tick_ms) <= max_gps_age_ms.
 * The unsigned subtraction handles a tick wrap; the caller must not retain a
 * fix across an entire uint32_t clock cycle. Use the default-age macro above
 * when constructing the input if no different age policy is required.
 *
 * A false gps_valid or stale fix emits gpsValid=false, null coordinates,
 * heading and accuracy, and speedKph=0.000. A fresh valid fix must also have
 * coordinates_valid, legal coordinates and speed, and a legal heading if
 * heading_valid. gpsAccuracy is always null. Coordinates use seven decimal
 * places and speed/heading use three, all using integer formatting.
 *
 * On success out is NUL-terminated and *written excludes that NUL. On every
 * failure *written is zero, and out[0] is NUL whenever out != NULL and
 * capacity > 0. No truncated JSON is returned as a successful message.
 */
rg_telemetry_status_t rg_telemetry_build_json(const rg_telemetry_input_t *input,
                                             char *out, size_t capacity,
                                             size_t *written);

#ifdef __cplusplus
}
#endif

#endif
