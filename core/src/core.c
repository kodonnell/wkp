#include "wkp/core.h"
#include "wkp/_version.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define WKP_MAX_DIMS 16
#define WKP_GEOM_VERSION 1
#define WKP_SEP_RING ','
#define WKP_SEP_MULTI ';'
#define WKP_HEADER_OFFSET 63
#define WKP_HEADER_MAX 63

typedef struct writer_t
{
    uint8_t *data;
    size_t capacity;
    size_t written;
    int overflow;
} writer_t;

static double factor_for_precision(int precision)
{
    int i;
    double v = 1.0;
    if (precision < 0 || precision > WKP_HEADER_MAX)
        return pow(10.0, (double)precision);
    for (i = 0; i < precision; ++i)
        v *= 10.0;
    return v;
}

static int decode_header_field(char c, int *out_value)
{
    int decoded;
    decoded = ((int)(unsigned char)c) - WKP_HEADER_OFFSET;
    if (decoded < 0 || decoded > WKP_HEADER_MAX)
        return 0;
    *out_value = decoded;
    return 1;
}

static int encode_header_field(int value, char *out_char)
{
    if (value < 0 || value > WKP_HEADER_MAX)
        return 0;
    *out_char = (char)(value + WKP_HEADER_OFFSET);
    return 1;
}

static int decode_once(const uint8_t *encoded, size_t encoded_size, size_t *index, long long *out_value)
{
    int shift;
    unsigned long long result;
    shift = 0;
    result = 0;
    for (;;)
    {
        int byte;
        if (*index >= encoded_size)
            return 0;
        byte = ((int)encoded[*index]) - 63;
        *index += 1;
        if (byte < 0)
            return 0;
        result |= ((unsigned long long)(byte & 0x1F)) << shift;
        shift += 5;
        if (shift > 63)
            return 0;
        if (byte < 0x20)
            break;
    }
    if ((result & 1ULL) != 0ULL)
        *out_value = (long long)(~(result >> 1ULL));
    else
        *out_value = (long long)(result >> 1ULL);
    return 1;
}

static void writer_push(writer_t *w, char c)
{
    if (w->written < w->capacity && w->data != NULL)
        w->data[w->written] = (uint8_t)c;
    else
        w->overflow = 1;
    w->written += 1;
}

static void writer_append_delta(writer_t *w, long long signed_value)
{
    unsigned long long v;
    v = (unsigned long long)(signed_value << 1);
    if (signed_value < 0)
        v = (unsigned long long)(~v);
    while (v >= 0x20ULL)
    {
        writer_push(w, (char)((0x20ULL | (v & 0x1FULL)) + 63ULL));
        v >>= 5ULL;
    }
    writer_push(w, (char)(v + 63ULL));
}

static int normalize_precisions(size_t dimensions, const int *precisions, size_t precision_count, int *out_precisions)
{
    size_t i;
    if (precision_count == 1)
    {
        for (i = 0; i < dimensions; ++i)
            out_precisions[i] = precisions[0];
        return 1;
    }
    if (precision_count == dimensions)
    {
        for (i = 0; i < dimensions; ++i)
            out_precisions[i] = precisions[i];
        return 1;
    }
    return 0;
}

static int grow_u8(wkp_context *ctx, size_t need)
{
    size_t new_cap;
    uint8_t *new_ptr;

    if (need <= ctx->u8_cap)
        return 1;
    new_cap = ctx->u8_cap == 0 ? 64 : ctx->u8_cap;
    while (new_cap < need)
        new_cap *= 2;

    new_ptr = (uint8_t *)realloc(ctx->u8, new_cap);
    if (new_ptr == NULL)
        return 0;

    ctx->u8 = new_ptr;
    ctx->u8_cap = new_cap;
    return 1;
}

static int grow_f64(wkp_context *ctx, size_t need)
{
    size_t new_cap;
    double *new_ptr;

    if (need <= ctx->f64_cap)
        return 1;
    new_cap = ctx->f64_cap == 0 ? 16 : ctx->f64_cap;
    while (new_cap < need)
        new_cap *= 2;

    new_ptr = (double *)realloc(ctx->f64, new_cap * sizeof(double));
    if (new_ptr == NULL)
        return 0;

    ctx->f64 = new_ptr;
    ctx->f64_cap = new_cap;
    return 1;
}

static int grow_sizes_a(wkp_context *ctx, size_t need)
{
    size_t new_cap;
    size_t *new_ptr;

    if (need <= ctx->sizes_a_cap)
        return 1;
    new_cap = ctx->sizes_a_cap == 0 ? 8 : ctx->sizes_a_cap;
    while (new_cap < need)
        new_cap *= 2;

    new_ptr = (size_t *)realloc(ctx->sizes_a, new_cap * sizeof(size_t));
    if (new_ptr == NULL)
        return 0;

    ctx->sizes_a = new_ptr;
    ctx->sizes_a_cap = new_cap;
    return 1;
}

static int grow_sizes_b(wkp_context *ctx, size_t need)
{
    size_t new_cap;
    size_t *new_ptr;

    if (need <= ctx->sizes_b_cap)
        return 1;
    new_cap = ctx->sizes_b_cap == 0 ? 4 : ctx->sizes_b_cap;
    while (new_cap < need)
        new_cap *= 2;

    new_ptr = (size_t *)realloc(ctx->sizes_b, new_cap * sizeof(size_t));
    if (new_ptr == NULL)
        return 0;

    ctx->sizes_b = new_ptr;
    ctx->sizes_b_cap = new_cap;
    return 1;
}

wkp_status wkp_context_init(wkp_context *ctx)
{
    if (ctx == NULL)
        return WKP_STATUS_INVALID_ARGUMENT;
    ctx->u8 = NULL;
    ctx->u8_cap = 0;
    ctx->f64 = NULL;
    ctx->f64_cap = 0;
    ctx->sizes_a = NULL;
    ctx->sizes_a_cap = 0;
    ctx->sizes_b = NULL;
    ctx->sizes_b_cap = 0;
    ctx->frame.version = 0;
    ctx->frame.precision = 0;
    ctx->frame.dimensions = 0;
    ctx->frame.geometry_type = 0;
    ctx->frame.coords = NULL;
    ctx->frame.coord_value_count = 0;
    ctx->frame.segment_point_counts = NULL;
    ctx->frame.segment_count = 0;
    ctx->frame.group_segment_counts = NULL;
    ctx->frame.group_count = 0;
    return WKP_STATUS_OK;
}

void wkp_context_free(wkp_context *ctx)
{
    if (ctx == NULL)
        return;
    free(ctx->u8);
    free(ctx->f64);
    free(ctx->sizes_a);
    free(ctx->sizes_b);
    ctx->u8 = NULL;
    ctx->u8_cap = 0;
    ctx->f64 = NULL;
    ctx->f64_cap = 0;
    ctx->sizes_a = NULL;
    ctx->sizes_a_cap = 0;
    ctx->sizes_b = NULL;
    ctx->sizes_b_cap = 0;
    ctx->frame.version = 0;
    ctx->frame.precision = 0;
    ctx->frame.dimensions = 0;
    ctx->frame.geometry_type = 0;
    ctx->frame.coords = NULL;
    ctx->frame.coord_value_count = 0;
    ctx->frame.segment_point_counts = NULL;
    ctx->frame.segment_count = 0;
    ctx->frame.group_segment_counts = NULL;
    ctx->frame.group_count = 0;
}

static wkp_status encode_f64_core(const double *values, size_t value_count, size_t dimensions, const int *precisions, size_t precision_count, uint8_t *out_data, size_t out_capacity, size_t *out_size)
{
    size_t i;
    int p[WKP_MAX_DIMS] = {0};
    double factors[WKP_MAX_DIMS] = {0.0};
    long long prev[WKP_MAX_DIMS] = {0};
    writer_t w;

    if (out_size == NULL || values == NULL || precisions == NULL || dimensions == 0 || dimensions > WKP_MAX_DIMS || value_count % dimensions != 0 || (out_data == NULL && out_capacity != 0) || precision_count == 0)
        return WKP_STATUS_INVALID_ARGUMENT;

    if (!normalize_precisions(dimensions, precisions, precision_count, p))
        return WKP_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < dimensions; ++i)
        factors[i] = factor_for_precision(p[i]);

    w.data = out_data;
    w.capacity = out_capacity;
    w.written = 0;
    w.overflow = 0;

    for (i = 0; i < value_count; ++i)
    {
        size_t dim = i % dimensions;
        long long scaled = (long long)llround(values[i] * factors[dim]);
        long long delta = scaled - prev[dim];
        prev[dim] = scaled;
        writer_append_delta(&w, delta);
    }

    *out_size = w.written;
    if (w.overflow)
        return WKP_STATUS_BUFFER_TOO_SMALL;
    return WKP_STATUS_OK;
}

static wkp_status decode_f64_core(const uint8_t *encoded, size_t encoded_size, size_t dimensions, const int *precisions, size_t precision_count, double *out_values, size_t out_capacity, size_t *out_size)
{
    size_t i = 0;
    size_t value_count = 0;
    int p[WKP_MAX_DIMS] = {0};
    double factors[WKP_MAX_DIMS] = {0.0};
    long long prev[WKP_MAX_DIMS] = {0};
    int overflow = 0;

    if (out_size == NULL || encoded == NULL || precisions == NULL || dimensions == 0 || dimensions > WKP_MAX_DIMS || (out_values == NULL && out_capacity != 0))
        return WKP_STATUS_INVALID_ARGUMENT;
    if (precision_count == 0)
        return WKP_STATUS_INVALID_ARGUMENT;
    if (!normalize_precisions(dimensions, precisions, precision_count, p))
        return WKP_STATUS_INVALID_ARGUMENT;

    size_t d;
    for (d = 0; d < dimensions; ++d)
        factors[d] = factor_for_precision(p[d]);

    while (i < encoded_size)
    {
        long long delta;
        size_t dim = value_count % dimensions;
        if (!decode_once(encoded, encoded_size, &i, &delta))
            return WKP_STATUS_MALFORMED_INPUT;
        prev[dim] += delta;
        if (value_count < out_capacity && out_values != NULL)
            out_values[value_count] = (double)prev[dim] / factors[dim];
        else
            overflow = 1;
        value_count++;
    }

    if ((value_count % dimensions) != 0)
        return WKP_STATUS_MALFORMED_INPUT;

    *out_size = value_count;
    if (overflow)
        return WKP_STATUS_BUFFER_TOO_SMALL;
    return WKP_STATUS_OK;
}

static wkp_status encode_geometry_frame_core(int geometry_type, const double *coords, size_t coord_value_count, size_t dimensions, int precision, const size_t *group_segment_counts, size_t group_count, const size_t *segment_point_counts, size_t segment_count, uint8_t *out_data, size_t out_capacity, size_t *out_size)
{
    size_t g;
    size_t s;
    double factors[WKP_MAX_DIMS] = {0.0};
    long long prev[WKP_MAX_DIMS] = {0};
    size_t coord_offset = 0;
    size_t seg_idx = 0;
    writer_t w;

    if (out_size == NULL || coords == NULL || group_segment_counts == NULL || segment_point_counts == NULL || (out_data == NULL && out_capacity != 0) || dimensions == 0 || dimensions > WKP_MAX_DIMS || (coord_value_count % dimensions) != 0 || precision < 0 || precision > WKP_HEADER_MAX || group_count == 0 || segment_count == 0 || geometry_type < 0 || geometry_type > WKP_HEADER_MAX)
        return WKP_STATUS_INVALID_ARGUMENT;

    size_t summed_segments = 0;
    size_t total_points = 0;
    for (g = 0; g < group_count; ++g)
        summed_segments += group_segment_counts[g];
    if (summed_segments != segment_count)
        return WKP_STATUS_INVALID_ARGUMENT;
    for (s = 0; s < segment_count; ++s)
        total_points += segment_point_counts[s];
    if (total_points * dimensions != coord_value_count)
        return WKP_STATUS_INVALID_ARGUMENT;

    for (s = 0; s < dimensions; ++s)
        factors[s] = factor_for_precision(precision);

    w.data = out_data;
    w.capacity = out_capacity;
    w.written = 0;
    w.overflow = 0;

    char h1, h2, h3, h4;
    if (!encode_header_field(WKP_GEOM_VERSION, &h1) || !encode_header_field(precision, &h2) || !encode_header_field((int)dimensions, &h3) || !encode_header_field(geometry_type, &h4))
        return WKP_STATUS_INVALID_ARGUMENT;
    writer_push(&w, h1);
    writer_push(&w, h2);
    writer_push(&w, h3);
    writer_push(&w, h4);

    for (g = 0; g < group_count; ++g)
    {
        size_t group_segments = group_segment_counts[g];
        if (g > 0)
            writer_push(&w, WKP_SEP_MULTI);
        for (s = 0; s < group_segments; ++s)
        {
            size_t p;
            size_t point_count = segment_point_counts[seg_idx++];
            size_t value_count = point_count * dimensions;
            if (s > 0)
                writer_push(&w, WKP_SEP_RING);
            for (p = 0; p < value_count; ++p)
            {
                size_t dim = p % dimensions;
                long long scaled = (long long)llround(coords[coord_offset + p] * factors[dim]);
                long long delta = scaled - prev[dim];
                prev[dim] = scaled;
                writer_append_delta(&w, delta);
            }
            coord_offset += value_count;
        }
    }

    *out_size = w.written;
    if (w.overflow)
        return WKP_STATUS_BUFFER_TOO_SMALL;
    return WKP_STATUS_OK;
}

static wkp_status decode_geometry_header_core(const uint8_t *encoded, size_t encoded_size, int *out_version, int *out_precision, int *out_dimensions, int *out_geometry_type)
{
    int v, p, d, t;
    if (encoded == NULL || out_version == NULL || out_precision == NULL || out_dimensions == NULL || out_geometry_type == NULL || encoded_size < 4)
        return WKP_STATUS_INVALID_ARGUMENT;

    if (!decode_header_field((char)encoded[0], &v) || v != WKP_GEOM_VERSION)
        return WKP_STATUS_INVALID_ARGUMENT;

    if (!decode_header_field((char)encoded[1], &p) || !decode_header_field((char)encoded[2], &d) || !decode_header_field((char)encoded[3], &t))
        return WKP_STATUS_INVALID_ARGUMENT;

    *out_version = v;
    *out_precision = p;
    *out_dimensions = d;
    *out_geometry_type = t;
    return WKP_STATUS_OK;
}

static wkp_status decode_geometry_frame_core(const uint8_t *encoded, size_t encoded_size, double *out_coords, size_t out_coord_value_capacity, size_t *out_coord_value_count, size_t *out_segment_point_counts, size_t out_segment_capacity, size_t *out_segment_count, size_t *out_group_segment_counts, size_t out_group_capacity, size_t *out_group_count, int *out_version, int *out_precision, int *out_dimensions, int *out_geometry_type)
{
    size_t i;
    size_t body_start = 4;
    size_t coord_offset = 0;
    size_t segment_offset = 0;
    size_t group_offset = 0;
    int version;
    int precision;
    int dimensions;
    int geometry_type;
    double factors[WKP_MAX_DIMS] = {0.0};
    long long prev[WKP_MAX_DIMS] = {0};
    int coord_overflow = 0;
    int segment_overflow = 0;
    int group_overflow = 0;

    if (encoded == NULL || out_coord_value_count == NULL || out_segment_count == NULL || out_group_count == NULL || out_version == NULL || out_precision == NULL || out_dimensions == NULL || out_geometry_type == NULL)
        return WKP_STATUS_INVALID_ARGUMENT;

    if (decode_geometry_header_core(encoded, encoded_size, &version, &precision, &dimensions, &geometry_type) != WKP_STATUS_OK)
        return WKP_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < (size_t)dimensions; ++i)
        factors[i] = factor_for_precision(precision);

    {
        size_t idx = body_start;
        size_t val_count = 0;
        size_t seg_in_group = 0;

        if (idx >= encoded_size)
            return WKP_STATUS_MALFORMED_INPUT;

        while (idx < encoded_size)
        {
            const uint8_t ch = encoded[idx];
            if (ch == (uint8_t)WKP_SEP_RING || ch == (uint8_t)WKP_SEP_MULTI)
            {
                if (val_count == 0)
                    return WKP_STATUS_MALFORMED_INPUT;
                if (val_count % (size_t)dimensions != 0)
                    return WKP_STATUS_MALFORMED_INPUT;

                if (segment_offset < out_segment_capacity && out_segment_point_counts != NULL)
                    out_segment_point_counts[segment_offset] = val_count / (size_t)dimensions;
                else
                    segment_overflow = 1;
                segment_offset++;
                seg_in_group++;
                val_count = 0;

                if (ch == (uint8_t)WKP_SEP_MULTI)
                {
                    if (seg_in_group == 0)
                        return WKP_STATUS_MALFORMED_INPUT;

                    if (group_offset < out_group_capacity && out_group_segment_counts != NULL)
                        out_group_segment_counts[group_offset] = seg_in_group;
                    else
                        group_overflow = 1;
                    group_offset++;
                    seg_in_group = 0;
                }

                idx++;
                continue;
            }

            {
                long long delta;
                size_t dim = val_count % (size_t)dimensions;
                if (!decode_once(encoded, encoded_size, &idx, &delta))
                    return WKP_STATUS_MALFORMED_INPUT;

                prev[dim] += delta;
                if (coord_offset < out_coord_value_capacity && out_coords != NULL)
                    out_coords[coord_offset] = (double)prev[dim] / factors[dim];
                else
                    coord_overflow = 1;
                coord_offset++;
                val_count++;
            }
        }

        if (val_count == 0)
            return WKP_STATUS_MALFORMED_INPUT;
        if (val_count % (size_t)dimensions != 0)
            return WKP_STATUS_MALFORMED_INPUT;

        if (segment_offset < out_segment_capacity && out_segment_point_counts != NULL)
            out_segment_point_counts[segment_offset] = val_count / (size_t)dimensions;
        else
            segment_overflow = 1;
        segment_offset++;
        seg_in_group++;

        if (group_offset < out_group_capacity && out_group_segment_counts != NULL)
            out_group_segment_counts[group_offset] = seg_in_group;
        else
            group_overflow = 1;
        group_offset++;
    }

    *out_coord_value_count = coord_offset;
    *out_segment_count = segment_offset;
    *out_group_count = group_offset;
    *out_version = version;
    *out_precision = precision;
    *out_dimensions = dimensions;
    *out_geometry_type = geometry_type;

    if (coord_overflow || segment_overflow || group_overflow)
        return WKP_STATUS_BUFFER_TOO_SMALL;
    return WKP_STATUS_OK;
}

wkp_status wkp_encode_f64(wkp_context *ctx, const double *values, size_t value_count, size_t dimensions, const int *precisions, size_t precision_count, const uint8_t **out_data, size_t *out_size)
{
    wkp_status s;

    if (ctx == NULL || out_data == NULL || out_size == NULL)
        return WKP_STATUS_INVALID_ARGUMENT;
    if (!grow_u8(ctx, value_count * 4 + 8))
        return WKP_STATUS_ALLOCATION_FAILED;

    while (1)
    {
        s = encode_f64_core(values, value_count, dimensions, precisions, precision_count,
                            ctx->u8, ctx->u8_cap, out_size);
        if (s == WKP_STATUS_OK)
        {
            *out_data = ctx->u8;
            return s;
        }
        if (s != WKP_STATUS_BUFFER_TOO_SMALL)
            return s;
        if (!grow_u8(ctx, *out_size))
            return WKP_STATUS_ALLOCATION_FAILED;
    }
}

wkp_status wkp_decode_f64(wkp_context *ctx, const uint8_t *encoded, size_t encoded_size, size_t dimensions, const int *precisions, size_t precision_count, const double **out_values, size_t *out_size)
{
    wkp_status s;

    if (ctx == NULL || out_values == NULL || out_size == NULL)
        return WKP_STATUS_INVALID_ARGUMENT;
    if (!grow_f64(ctx, encoded_size > 16 ? encoded_size : 16))
        return WKP_STATUS_ALLOCATION_FAILED;

    while (1)
    {
        s = decode_f64_core(encoded, encoded_size, dimensions, precisions, precision_count,
                            ctx->f64, ctx->f64_cap, out_size);
        if (s == WKP_STATUS_OK)
        {
            *out_values = ctx->f64;
            return s;
        }
        if (s != WKP_STATUS_BUFFER_TOO_SMALL)
            return s;
        if (!grow_f64(ctx, *out_size))
            return WKP_STATUS_ALLOCATION_FAILED;
    }
}

wkp_status wkp_encode_geometry_frame(wkp_context *ctx, int geometry_type, const double *coords, size_t coord_value_count, size_t dimensions, int precision, const size_t *group_segment_counts, size_t group_count, const size_t *segment_point_counts, size_t segment_count, const uint8_t **out_data, size_t *out_size)
{
    wkp_status s;

    if (ctx == NULL || out_data == NULL || out_size == NULL)
        return WKP_STATUS_INVALID_ARGUMENT;
    if (!grow_u8(ctx, 64))
        return WKP_STATUS_ALLOCATION_FAILED;

    while (1)
    {
        s = encode_geometry_frame_core(
            geometry_type, coords, coord_value_count, dimensions, precision,
            group_segment_counts, group_count, segment_point_counts, segment_count,
            ctx->u8, ctx->u8_cap, out_size);
        if (s == WKP_STATUS_OK)
        {
            *out_data = ctx->u8;
            return s;
        }
        if (s != WKP_STATUS_BUFFER_TOO_SMALL)
            return s;
        if (!grow_u8(ctx, *out_size))
            return WKP_STATUS_ALLOCATION_FAILED;
    }
}

wkp_status wkp_decode_geometry_frame(wkp_context *ctx, const uint8_t *encoded, size_t encoded_size, const wkp_geometry_frame_f64 **out_frame)
{
    wkp_status s;
    size_t coord_count = 0;
    size_t seg_count = 0;
    size_t group_count = 0;
    int version = 0;
    int precision = 0;
    int dimensions = 0;
    int geometry_type = 0;

    if (ctx == NULL || out_frame == NULL)
        return WKP_STATUS_INVALID_ARGUMENT;
    if (!grow_f64(ctx, encoded_size > 16 ? encoded_size : 16) ||
        !grow_sizes_a(ctx, 8) ||
        !grow_sizes_b(ctx, 4))
        return WKP_STATUS_ALLOCATION_FAILED;

    while (1)
    {
        s = decode_geometry_frame_core(
            encoded, encoded_size,
            ctx->f64, ctx->f64_cap, &coord_count,
            ctx->sizes_a, ctx->sizes_a_cap, &seg_count,
            ctx->sizes_b, ctx->sizes_b_cap, &group_count,
            &version, &precision, &dimensions, &geometry_type);

        if (s == WKP_STATUS_OK)
        {
            ctx->frame.version = version;
            ctx->frame.precision = precision;
            ctx->frame.dimensions = dimensions;
            ctx->frame.geometry_type = geometry_type;
            ctx->frame.coords = coord_count ? ctx->f64 : NULL;
            ctx->frame.coord_value_count = coord_count;
            ctx->frame.segment_point_counts = seg_count ? ctx->sizes_a : NULL;
            ctx->frame.segment_count = seg_count;
            ctx->frame.group_segment_counts = group_count ? ctx->sizes_b : NULL;
            ctx->frame.group_count = group_count;
            *out_frame = &ctx->frame;
            return s;
        }

        if (s != WKP_STATUS_BUFFER_TOO_SMALL)
            return s;

        if (!grow_f64(ctx, coord_count) ||
            !grow_sizes_a(ctx, seg_count) ||
            !grow_sizes_b(ctx, group_count))
            return WKP_STATUS_ALLOCATION_FAILED;
    }
}

wkp_status wkp_decode_geometry_header(const uint8_t *encoded, size_t encoded_size, int *out_version, int *out_precision, int *out_dimensions, int *out_geometry_type)
{
    return decode_geometry_header_core(
        encoded,
        encoded_size,
        out_version,
        out_precision,
        out_dimensions,
        out_geometry_type);
}

wkp_status wkp_basic_self_test(int *out_failed_check)
{
    wkp_context ctx;
    wkp_status s;
    const double values[] = {38.5, -120.2, 40.7, -120.95, 43.252, -126.453};
    const int precisions[] = {5};
    const size_t groups[] = {1};
    const size_t segments[] = {3};
    const char expected_floats[] = "_p~iF~ps|U_ulLnnqC_mqNvxq`@";
    const char expected_geometry[] = "@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@";
    const uint8_t *encoded = NULL;
    size_t encoded_size = 0;
    const double *decoded = NULL;
    size_t decoded_size = 0;
    const wkp_geometry_frame_f64 *frame = NULL;
    int version = 0;
    int precision = 0;
    int dimensions = 0;
    int geometry_type = 0;
    size_t i;

    if (out_failed_check != NULL)
        *out_failed_check = 0;

    s = wkp_context_init(&ctx);
    if (s != WKP_STATUS_OK)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 1;
        return s;
    }

    s = wkp_encode_f64(
        &ctx,
        values,
        sizeof(values) / sizeof(values[0]),
        2,
        precisions,
        1,
        &encoded,
        &encoded_size);
    if (s != WKP_STATUS_OK)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 2;
        wkp_context_free(&ctx);
        return s;
    }

    if (encoded_size != sizeof(expected_floats) - 1)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 3;
        wkp_context_free(&ctx);
        return WKP_STATUS_INTERNAL_ERROR;
    }

    for (i = 0; i < encoded_size; ++i)
    {
        if (encoded[i] != (uint8_t)expected_floats[i])
        {
            if (out_failed_check != NULL)
                *out_failed_check = 4;
            wkp_context_free(&ctx);
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    s = wkp_decode_f64(
        &ctx,
        encoded,
        encoded_size,
        2,
        precisions,
        1,
        &decoded,
        &decoded_size);
    if (s != WKP_STATUS_OK)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 5;
        wkp_context_free(&ctx);
        return s;
    }

    if (decoded_size != sizeof(values) / sizeof(values[0]))
    {
        if (out_failed_check != NULL)
            *out_failed_check = 6;
        wkp_context_free(&ctx);
        return WKP_STATUS_INTERNAL_ERROR;
    }

    for (i = 0; i < decoded_size; ++i)
    {
        if (fabs(decoded[i] - values[i]) > 1e-9)
        {
            if (out_failed_check != NULL)
                *out_failed_check = 7;
            wkp_context_free(&ctx);
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    s = wkp_encode_geometry_frame(
        &ctx,
        WKP_GEOMETRY_LINESTRING,
        values,
        sizeof(values) / sizeof(values[0]),
        2,
        5,
        groups,
        1,
        segments,
        1,
        &encoded,
        &encoded_size);
    if (s != WKP_STATUS_OK)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 8;
        wkp_context_free(&ctx);
        return s;
    }

    if (encoded_size != sizeof(expected_geometry) - 1)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 9;
        wkp_context_free(&ctx);
        return WKP_STATUS_INTERNAL_ERROR;
    }

    for (i = 0; i < encoded_size; ++i)
    {
        if (encoded[i] != (uint8_t)expected_geometry[i])
        {
            if (out_failed_check != NULL)
                *out_failed_check = 10;
            wkp_context_free(&ctx);
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    s = wkp_decode_geometry_header(
        encoded,
        encoded_size,
        &version,
        &precision,
        &dimensions,
        &geometry_type);
    if (s != WKP_STATUS_OK)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 11;
        wkp_context_free(&ctx);
        return s;
    }

    if (version != 1 || precision != 5 || dimensions != 2 || geometry_type != WKP_GEOMETRY_LINESTRING)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 12;
        wkp_context_free(&ctx);
        return WKP_STATUS_INTERNAL_ERROR;
    }

    s = wkp_decode_geometry_frame(&ctx, encoded, encoded_size, &frame);
    if (s != WKP_STATUS_OK)
    {
        if (out_failed_check != NULL)
            *out_failed_check = 13;
        wkp_context_free(&ctx);
        return s;
    }

    if (frame == NULL || frame->coord_value_count != sizeof(values) / sizeof(values[0]))
    {
        if (out_failed_check != NULL)
            *out_failed_check = 14;
        wkp_context_free(&ctx);
        return WKP_STATUS_INTERNAL_ERROR;
    }

    wkp_context_free(&ctx);
    return WKP_STATUS_OK;
}
