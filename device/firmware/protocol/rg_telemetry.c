#include "rg_telemetry.h"

typedef struct
{
    char *out;
    size_t capacity;
    size_t length;
} json_writer_t;

static int id_is_valid(const char *id, size_t minimum, size_t maximum)
{
    size_t length = 0U;
    if (id == NULL) return 0;

    while (id[length] != '\0')
    {
        unsigned char c;
        if (length >= maximum) return 0;
        c = (unsigned char)id[length];
        if (!((c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
              (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
              (c >= (unsigned char)'0' && c <= (unsigned char)'9') ||
              c == (unsigned char)'_' || c == (unsigned char)'-')) return 0;
        length++;
    }
    return length >= minimum;
}

static void put_char(json_writer_t *writer, char value)
{
    if (writer->length < writer->capacity - 1U)
        writer->out[writer->length] = value;
    writer->length++;
}

static void put_text(json_writer_t *writer, const char *text)
{
    while (*text != '\0') put_char(writer, *text++);
}

static void put_unsigned(json_writer_t *writer, uint64_t value)
{
    char digits[20];
    size_t count = 0U;
    do
    {
        digits[count++] = (char)('0' + (unsigned int)(value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (count != 0U) put_char(writer, digits[--count]);
}

static void put_fixed(json_writer_t *writer, int64_t value,
                      uint32_t scale, unsigned int decimal_places)
{
    uint64_t magnitude;
    uint32_t fraction;
    uint32_t divisor = scale / 10U;

    if (value < 0)
    {
        put_char(writer, '-');
        magnitude = (uint64_t)(-(value + 1)) + 1U;
    }
    else
    {
        magnitude = (uint64_t)value;
    }
    put_unsigned(writer, magnitude / scale);
    put_char(writer, '.');
    fraction = (uint32_t)(magnitude % scale);
    while (decimal_places != 0U)
    {
        put_char(writer, (char)('0' + fraction / divisor));
        fraction %= divisor;
        divisor /= 10U;
        decimal_places--;
    }
}

rg_telemetry_status_t rg_telemetry_build_json(const rg_telemetry_input_t *input,
                                             char *out, size_t capacity,
                                             size_t *written)
{
    const rg_gps_fix_t *fix;
    int use_fix;
    json_writer_t writer;

    if (written != NULL) *written = 0U;
    if (out != NULL && capacity > 0U) out[0] = '\0';
    if (input == NULL || out == NULL || input->gps == NULL)
        return RG_TELEMETRY_INVALID_ARGUMENT;
    if (capacity == 0U) return RG_TELEMETRY_BUFFER_TOO_SMALL;
    if (!id_is_valid(input->device_id, RG_TELEMETRY_DEVICE_ID_MIN_LENGTH,
                     RG_TELEMETRY_DEVICE_ID_MAX_LENGTH) ||
        !id_is_valid(input->sample_id, 1U, RG_TELEMETRY_SAMPLE_ID_MAX_LENGTH))
        return RG_TELEMETRY_INVALID_ID;
    if (input->captured_at_ms <= 0) return RG_TELEMETRY_INVALID_TIMESTAMP;

    fix = input->gps;
    use_fix = fix->gps_valid &&
        (uint32_t)(input->now_tick_ms - fix->receipt_tick_ms) <= input->max_gps_age_ms;
    if (use_fix && (!fix->coordinates_valid ||
        fix->latitude_e7 < -900000000 || fix->latitude_e7 > 900000000 ||
        fix->longitude_e7 < -1800000000 || fix->longitude_e7 > 1800000000 ||
        fix->speed_milli_kph > 150000U ||
        (fix->heading_valid && fix->heading_milli_deg >= 360000U)))
        return RG_TELEMETRY_INVALID_FIX;

    writer.out = out;
    writer.capacity = capacity;
    writer.length = 0U;
    put_text(&writer, "{\"deviceId\":\"");
    put_text(&writer, input->device_id);
    put_text(&writer, "\",\"sampleId\":\"");
    put_text(&writer, input->sample_id);
    put_text(&writer, "\",\"capturedAtMs\":");
    put_unsigned(&writer, (uint64_t)input->captured_at_ms);
    put_text(&writer, ",\"latitude\":");
    if (use_fix) put_fixed(&writer, fix->latitude_e7, 10000000U, 7U);
    else put_text(&writer, "null");
    put_text(&writer, ",\"longitude\":");
    if (use_fix) put_fixed(&writer, fix->longitude_e7, 10000000U, 7U);
    else put_text(&writer, "null");
    put_text(&writer, ",\"speedKph\":");
    if (use_fix) put_fixed(&writer, fix->speed_milli_kph, 1000U, 3U);
    else put_text(&writer, "0.000");
    put_text(&writer, ",\"heading\":");
    if (use_fix && fix->heading_valid)
        put_fixed(&writer, fix->heading_milli_deg, 1000U, 3U);
    else put_text(&writer, "null");
    put_text(&writer, ",\"gpsAccuracy\":null,\"gpsValid\":");
    put_text(&writer, use_fix ? "true}" : "false}");

    if (writer.length >= capacity)
    {
        out[0] = '\0';
        return RG_TELEMETRY_BUFFER_TOO_SMALL;
    }
    out[writer.length] = '\0';
    if (written != NULL) *written = writer.length;
    return RG_TELEMETRY_OK;
}
