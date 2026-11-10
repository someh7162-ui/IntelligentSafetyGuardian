#include "rg_gps.h"

#include <stddef.h>
#include <string.h>

#define RG_MAX_INPUT 255U
#define RG_MAX_RMC_FIELDS 20U

typedef struct
{
    const char *text;
    size_t length;
} span_t;

typedef struct
{
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int millis;
} clock_t;

typedef struct
{
    unsigned int year;
    unsigned int month;
    unsigned int day;
} date_t;

static void invalid_fix(rg_gps_fix_t *out)
{
    memset(out, 0, sizeof(*out));
    out->latitude_e7 = RG_GPS_COORD_INVALID;
    out->longitude_e7 = RG_GPS_COORD_INVALID;
    out->heading_milli_deg = RG_GPS_HEADING_INVALID;
    out->unix_ms = RG_GPS_UTC_INVALID;
}

static int bounded_length(const char *text, size_t *length)
{
    size_t index;
    if (text == NULL) return 0;
    for (index = 0U; index <= RG_MAX_INPUT; index++)
    {
        if (text[index] == '\0')
        {
            *length = index;
            return 1;
        }
    }
    return 0;
}

static int digit(char value)
{
    return value >= '0' && value <= '9';
}

static int hex_value(char value)
{
    if (digit(value)) return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static int parse_two(const char *text, unsigned int *number)
{
    if (!digit(text[0]) || !digit(text[1])) return 0;
    *number = (unsigned int)(text[0] - '0') * 10U +
              (unsigned int)(text[1] - '0');
    return 1;
}

static int leap_year(unsigned int year)
{
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

static unsigned int month_days(unsigned int year, unsigned int month)
{
    static const unsigned int days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    if (month < 1U || month > 12U) return 0U;
    return days[month - 1U] + ((month == 2U && leap_year(year)) ? 1U : 0U);
}

static int valid_date(const date_t *date)
{
    return date->year >= 1970U && date->year <= 2099U &&
           date->month >= 1U && date->month <= 12U && date->day >= 1U &&
           date->day <= month_days(date->year, date->month);
}

static int64_t civil_unix_ms(const date_t *date, const clock_t *clock)
{
    unsigned int year;
    unsigned int month;
    int64_t days = 0;
    for (year = 1970U; year < date->year; year++)
        days += leap_year(year) ? 366 : 365;
    for (month = 1U; month < date->month; month++)
        days += (int64_t)month_days(date->year, month);
    days += (int64_t)date->day - 1;
    return (days * INT64_C(86400) + (int64_t)clock->hour * 3600 +
            (int64_t)clock->minute * 60 + (int64_t)clock->second) * 1000 +
           (int64_t)clock->millis;
}

static int parse_rmc_date(span_t field, date_t *date)
{
    unsigned int year;
    if (field.length != 6U || !parse_two(field.text, &date->day) ||
        !parse_two(field.text + 2U, &date->month) ||
        !parse_two(field.text + 4U, &year)) return 0;
    date->year = year >= 80U ? 1900U + year : 2000U + year;
    return valid_date(date);
}

static int parse_rmc_clock(span_t field, clock_t *clock)
{
    size_t index;
    unsigned int multiplier = 100U;
    if (field.length < 6U || !parse_two(field.text, &clock->hour) ||
        !parse_two(field.text + 2U, &clock->minute) ||
        !parse_two(field.text + 4U, &clock->second) ||
        clock->hour > 23U || clock->minute > 59U || clock->second > 59U) return 0;
    clock->millis = 0U;
    if (field.length == 6U) return 1;
    if (field.text[6] != '.' || field.length < 8U || field.length > 16U) return 0;
    for (index = 7U; index < field.length; index++)
    {
        if (!digit(field.text[index])) return 0;
        if (multiplier != 0U)
        {
            clock->millis += (unsigned int)(field.text[index] - '0') * multiplier;
            multiplier /= 10U;
        }
    }
    return 1;
}

/* Parse a bounded unsigned decimal as numerator / scale. */
static int decimal(span_t field, unsigned int max_whole,
                   uint64_t *numerator, uint64_t *scale)
{
    size_t index = 0U;
    size_t fractional_digits = 0U;
    unsigned int whole = 0U;
    uint64_t fraction = 0U;
    uint64_t denominator = 1U;
    if (field.length == 0U || !digit(field.text[0])) return 0;
    while (index < field.length && digit(field.text[index]))
    {
        unsigned int value = (unsigned int)(field.text[index] - '0');
        if (whole > max_whole / 10U ||
            (whole == max_whole / 10U && value > max_whole % 10U)) return 0;
        whole = whole * 10U + value;
        index++;
    }
    if (index != field.length)
    {
        if (field.text[index++] != '.' || index == field.length) return 0;
        while (index < field.length)
        {
            if (!digit(field.text[index]) || ++fractional_digits > 9U) return 0;
            fraction = fraction * 10U + (uint64_t)(field.text[index++] - '0');
            denominator *= 10U;
        }
    }
    *numerator = (uint64_t)whole * denominator + fraction;
    *scale = denominator;
    return 1;
}

static int coordinate(span_t number, span_t hemisphere, int latitude,
                      int32_t *coordinate_e7)
{
    unsigned int degree_digits = latitude ? 2U : 3U;
    unsigned int degrees = 0U;
    unsigned int minutes;
    unsigned int maximum = latitude ? 90U : 180U;
    size_t index;
    span_t minute_field;
    uint64_t numerator;
    uint64_t scale;
    uint64_t absolute;
    if (hemisphere.length != 1U || number.length < degree_digits + 2U) return 0;
    if (latitude ? (hemisphere.text[0] != 'N' && hemisphere.text[0] != 'S') :
                   (hemisphere.text[0] != 'E' && hemisphere.text[0] != 'W')) return 0;
    for (index = 0U; index < degree_digits; index++)
    {
        if (!digit(number.text[index])) return 0;
        degrees = degrees * 10U + (unsigned int)(number.text[index] - '0');
    }
    if (degrees > maximum || !parse_two(number.text + degree_digits, &minutes) ||
        minutes >= 60U) return 0;
    if (number.length > degree_digits + 2U && number.text[degree_digits + 2U] != '.') return 0;
    minute_field.text = number.text + degree_digits;
    minute_field.length = number.length - degree_digits;
    if (!decimal(minute_field, 59U, &numerator, &scale)) return 0;
    if (degrees == maximum && numerator != 0U) return 0;
    absolute = (uint64_t)degrees * UINT64_C(10000000) +
               (numerator * UINT64_C(10000000) + 30U * scale) / (60U * scale);
    *coordinate_e7 = (int32_t)absolute;
    if (hemisphere.text[0] == 'S' || hemisphere.text[0] == 'W')
        *coordinate_e7 = -*coordinate_e7;
    return 1;
}

static int speed(span_t field, uint32_t *milli_kph)
{
    uint64_t numerator;
    uint64_t scale;
    if (!decimal(field, 150U, &numerator, &scale)) return 0;
    /* One knot is exactly 1.852 km/h. Check the exact value before rounding. */
    if (numerator * 1852U > UINT64_C(150000) * scale) return 0;
    *milli_kph = (uint32_t)((numerator * 1852U + scale / 2U) / scale);
    return 1;
}

static int heading(span_t field, uint32_t *milli_deg)
{
    uint64_t numerator;
    uint64_t scale;
    if (!decimal(field, 359U, &numerator, &scale)) return 0;
    *milli_deg = (uint32_t)(numerator * 1000U / scale);
    return 1;
}

int rg_gps_parse_rmc(const char *sentence, uint32_t receipt_tick_ms,
                     rg_gps_fix_t *out)
{
    size_t length;
    size_t star;
    size_t index;
    size_t field_start;
    size_t count = 0U;
    unsigned int checksum = 0U;
    int high;
    int low;
    int valid;
    span_t fields[RG_MAX_RMC_FIELDS];
    clock_t clock = {0U, 0U, 0U, 0U};
    date_t date = {0U, 0U, 0U};
    rg_gps_fix_t parsed;
    if (out == NULL) return 0;
    invalid_fix(out);
    invalid_fix(&parsed);
    if (!bounded_length(sentence, &length)) return 0;
    if (length >= 2U && sentence[length - 2U] == '\r' && sentence[length - 1U] == '\n') length -= 2U;
    if (length < 10U || sentence[0] != '$') return 0;
    for (star = 1U; star < length && sentence[star] != '*'; star++)
        checksum ^= (unsigned char)sentence[star];
    if (star + 3U != length) return 0;
    high = hex_value(sentence[star + 1U]);
    low = hex_value(sentence[star + 2U]);
    if (high < 0 || low < 0 || checksum != (unsigned int)(high * 16 + low)) return 0;

    field_start = 1U;
    for (index = 1U; index <= star; index++)
    {
        if (index == star || sentence[index] == ',')
        {
            if (count >= RG_MAX_RMC_FIELDS) return 0;
            fields[count].text = sentence + field_start;
            fields[count].length = index - field_start;
            count++;
            field_start = index + 1U;
        }
    }
    if (count < 10U || fields[0].length != 5U ||
        fields[0].text[0] < 'A' || fields[0].text[0] > 'Z' ||
        fields[0].text[1] < 'A' || fields[0].text[1] > 'Z' ||
        memcmp(fields[0].text + 2U, "RMC", 3U) != 0 || fields[2].length != 1U ||
        (fields[2].text[0] != 'A' && fields[2].text[0] != 'V')) return 0;
    valid = fields[2].text[0] == 'A';

    if ((fields[1].length != 0U && !parse_rmc_clock(fields[1], &clock)) ||
        (fields[9].length != 0U && !parse_rmc_date(fields[9], &date))) return 0;
    if (fields[1].length != 0U && fields[9].length != 0U)
    {
        parsed.utc_valid = true;
        parsed.unix_ms = civil_unix_ms(&date, &clock);
    }
    if (valid && !parsed.utc_valid) return 0;

    if (valid || fields[3].length != 0U || fields[4].length != 0U ||
        fields[5].length != 0U || fields[6].length != 0U)
    {
        if (!coordinate(fields[3], fields[4], 1, &parsed.latitude_e7) ||
            !coordinate(fields[5], fields[6], 0, &parsed.longitude_e7)) return 0;
    }
    if ((valid || fields[7].length != 0U) && !speed(fields[7], &parsed.speed_milli_kph)) return 0;
    if (fields[8].length != 0U)
    {
        if (!heading(fields[8], &parsed.heading_milli_deg)) return 0;
        parsed.heading_valid = true;
    }

    parsed.gps_valid = valid != 0;
    parsed.coordinates_valid = parsed.gps_valid;
    parsed.receipt_tick_ms = receipt_tick_ms;
    if (!valid)
    {
        parsed.latitude_e7 = RG_GPS_COORD_INVALID;
        parsed.longitude_e7 = RG_GPS_COORD_INVALID;
        parsed.speed_milli_kph = 0U;
        parsed.heading_milli_deg = RG_GPS_HEADING_INVALID;
        parsed.heading_valid = false;
    }
    *out = parsed;
    return 1;
}

static int outer_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

int rg_gps_parse_cclk(const char *text, int64_t *unix_ms)
{
    size_t length;
    const char *value;
    date_t date;
    clock_t clock = {0U, 0U, 0U, 0U};
    unsigned int year;
    unsigned int quarters;
    int offset;
    int64_t timestamp;
    if (unix_ms == NULL) return 0;
    *unix_ms = RG_GPS_UTC_INVALID;
    if (!bounded_length(text, &length)) return 0;
    value = text;
    while (length != 0U && outer_space(*value)) { value++; length--; }
    while (length != 0U && outer_space(value[length - 1U])) length--;
    if (length >= 6U && memcmp(value, "+CCLK:", 6U) == 0)
    {
        value += 6U;
        length -= 6U;
        while (length != 0U && (*value == ' ' || *value == '\t')) { value++; length--; }
        if (length < 2U || value[0] != '"' || value[length - 1U] != '"') return 0;
    }
    if (length >= 2U && value[0] == '"' && value[length - 1U] == '"')
    {
        value++;
        length -= 2U;
    }
    if (length != 20U || value[2] != '/' || value[5] != '/' || value[8] != ',' ||
        value[11] != ':' || value[14] != ':' ||
        (value[17] != '+' && value[17] != '-')) return 0;
    if (!parse_two(value, &year) || !parse_two(value + 3U, &date.month) ||
        !parse_two(value + 6U, &date.day) || !parse_two(value + 9U, &clock.hour) ||
        !parse_two(value + 12U, &clock.minute) || !parse_two(value + 15U, &clock.second) ||
        !parse_two(value + 18U, &quarters)) return 0;
    date.year = 2000U + year;
    offset = value[17] == '-' ? -(int)quarters : (int)quarters;
    if (!valid_date(&date) || clock.hour > 23U || clock.minute > 59U ||
        clock.second > 59U || offset < -47 || offset > 48) return 0;
    timestamp = civil_unix_ms(&date, &clock) - (int64_t)offset * INT64_C(900000);
    if (timestamp < 0) return 0;
    *unix_ms = timestamp;
    return 1;
}
