#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum wkp_status
    {
        WKP_STATUS_OK = 0,
        WKP_STATUS_INVALID_ARGUMENT = 1,
        WKP_STATUS_MALFORMED_INPUT = 2,
        WKP_STATUS_ALLOCATION_FAILED = 3,
        WKP_STATUS_BUFFER_TOO_SMALL = 4,
        WKP_STATUS_LIMIT_EXCEEDED = 5,
        WKP_STATUS_INTERNAL_ERROR = 255
    } wkp_status;

    typedef struct wkp_size_buffer
    {
        size_t *data;
        size_t size;
    } wkp_size_buffer;

    typedef enum wkp_geometry_type
    {
        WKP_GEOMETRY_POINT = 1,
        WKP_GEOMETRY_LINESTRING = 2,
        WKP_GEOMETRY_POLYGON = 3,
        WKP_GEOMETRY_MULTIPOINT = 4,
        WKP_GEOMETRY_MULTILINESTRING = 5,
        WKP_GEOMETRY_MULTIPOLYGON = 6
    } wkp_geometry_type;

    typedef struct wkp_geometry_frame_f64
    {
        int version;
        int precision;
        int dimensions;
        int geometry_type;
        const double *coords;
        size_t coord_value_count;
        const size_t *segment_point_counts;
        size_t segment_count;
        const size_t *group_segment_counts;
        size_t group_count;
    } wkp_geometry_frame_f64;

    // Reusable caller-owned buffers for convenience wrapper APIs.
    // Callers must use one context per thread when using wrappers concurrently.
    typedef struct wkp_context
    {
        uint8_t *u8;
        size_t u8_cap;
        double *f64;
        size_t f64_cap;
        size_t *sizes_a;
        size_t sizes_a_cap;
        size_t *sizes_b;
        size_t sizes_b_cap;
        wkp_geometry_frame_f64 frame;
    } wkp_context;

    wkp_status wkp_context_init(
        wkp_context *ctx);

    void wkp_context_free(
        wkp_context *ctx);

    // Utility APIs that auto-resize buffers inside the provided context on BUFFER_TOO_SMALL.
    wkp_status wkp_encode_f64(
        wkp_context *ctx,
        const double *values,
        size_t value_count,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const uint8_t **out_data,
        size_t *out_size);

    wkp_status wkp_decode_f64(
        wkp_context *ctx,
        const uint8_t *encoded,
        size_t encoded_size,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const double **out_values,
        size_t *out_size);

    wkp_status wkp_encode_geometry_frame(
        wkp_context *ctx,
        int geometry_type,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *group_segment_counts,
        size_t group_count,
        const size_t *segment_point_counts,
        size_t segment_count,
        const uint8_t **out_data,
        size_t *out_size);

    wkp_status wkp_decode_geometry_frame(
        wkp_context *ctx,
        const uint8_t *encoded,
        size_t encoded_size,
        const wkp_geometry_frame_f64 **out_frame);

    wkp_status wkp_decode_geometry_header(
        const uint8_t *encoded,
        size_t encoded_size,
        int *out_version,
        int *out_precision,
        int *out_dimensions,
        int *out_geometry_type);

    // Runs a minimal built-in self-test suite for core encode/decode paths.
    // Returns WKP_STATUS_OK when all checks pass.
    // If non-NULL, out_failed_check receives a 1-based check index for failures.
    wkp_status wkp_basic_self_test(
        int *out_failed_check);

#ifdef __cplusplus
}
#endif
