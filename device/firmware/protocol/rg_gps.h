#ifndef RG_GPS_H
#define RG_GPS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RG_GPS_COORD_INVALID INT32_MIN
#define RG_GPS_HEADING_INVALID UINT32_MAX
#define RG_GPS_UTC_INVALID INT64_MIN

typedef struct
{
    bool gps_valid;
    bool utc_valid;
    bool heading_valid;
    bool coordinates_valid;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t speed_milli_kph;
    uint32_t heading_milli_deg;
    int64_t unix_ms;
    uint32_t receipt_tick_ms;
} rg_gps_fix_t;

/*
 * Return 1 for a structurally valid, checksum-valid RMC (including status V),
 * otherwise 0. Input must be a NUL-terminated complete sentence, optionally
 * followed by CR LF. Talker IDs consist of two uppercase ASCII letters.
 *
 * Status A requires complete UTC/date, coordinates and speed. Status V permits
 * empty UTC/date and navigation fields; nonempty fields are still validated.
 * A V result never exposes coordinates/heading and always has speed zero.
 * receipt_tick_ms is caller-supplied monotonic receipt time, not Unix time.
 * Freshness policy belongs to the caller. On failure out is cleared to invalid
 * sentinels and receipt_tick_ms=0; do not replace a stored fix on failure.
 *
 * RMC yy years use pivot 80: 80..99 -> 1980..1999, 00..79 -> 2000..2079.
 * Fractional UTC seconds are truncated to milliseconds. Coordinates are rounded
 * to 1e-7 degrees; speed rounds to milli-km/h; heading truncates to milli-degrees.
 * Numeric fields accept unsigned decimal digits with at most nine fractional
 * digits, never signs, whitespace, exponents, NaN or Inf.
 */
int rg_gps_parse_rmc(const char *sentence, uint32_t receipt_tick_ms,
                     rg_gps_fix_t *out);

/*
 * Parse Air780EPM CCLK local time yy/MM/dd,hh:mm:ss+zz or ...-zz, where zz is
 * quarter-hours and the module-documented signed range is -47..+48.
 * UTC = local time - offset. CCLK yy is always 2000..2099.
 * Accept a bare value, a quoted value, or +CCLK: "value", with optional outer
 * ASCII whitespace. Reject absent timezone, multiple response lines, fractional
 * seconds, invalid calendar dates, leap seconds and out-of-range timezones.
 * On failure *unix_ms is RG_GPS_UTC_INVALID. This validates representation,
 * not the trustworthiness/freshness of the module's clock.
 */
int rg_gps_parse_cclk(const char *text, int64_t *unix_ms);

#ifdef __cplusplus
}
#endif

#endif
