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

    typedef struct wkp_workspace wkp_workspace;

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

    wkp_status wkp_workspace_create(
        size_t initial_u8_capacity,
        size_t initial_f64_capacity,
        int64_t max_u8_capacity,
        int64_t max_f64_capacity,
        wkp_workspace **out_workspace,
        char *error_message,
        size_t error_message_capacity);

    void wkp_workspace_destroy(wkp_workspace *workspace);

    wkp_status wkp_workspace_encode_f64(
        wkp_workspace *workspace,
        const double *values,
        size_t value_count,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity);

    wkp_status wkp_workspace_decode_f64(
        wkp_workspace *workspace,
        const uint8_t *encoded,
        size_t encoded_size,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const double **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity);

    wkp_status wkp_workspace_encode_geometry_frame_f64(
        wkp_workspace *workspace,
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
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity);

    wkp_status wkp_workspace_decode_geometry_frame_f64(
        wkp_workspace *workspace,
        const uint8_t *encoded,
        size_t encoded_size,
        const wkp_geometry_frame_f64 **out_frame,
        char *error_message,
        size_t error_message_capacity);

    wkp_status wkp_decode_geometry_header(
        const uint8_t *encoded,
        size_t encoded_size,
        int *out_version,
        int *out_precision,
        int *out_dimensions,
        int *out_geometry_type,
        char *error_message,
        size_t error_message_capacity);

#ifdef __cplusplus
}
#endif
