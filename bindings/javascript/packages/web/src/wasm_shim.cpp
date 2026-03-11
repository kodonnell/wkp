#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#include "wkp/_version.h"
#include "wkp/core.h"

extern "C"
{

    int wkp_wasm_workspace_create(
        size_t initial_u8_capacity,
        size_t initial_f64_capacity,
        int32_t max_u8_capacity,
        int32_t max_f64_capacity,
        wkp_context **out_workspace,
        char *error_message,
        size_t error_message_capacity)
    {
        (void)initial_u8_capacity;
        (void)initial_f64_capacity;
        (void)max_u8_capacity;
        (void)max_f64_capacity;
        (void)error_message;
        (void)error_message_capacity;

        if (out_workspace == nullptr)
        {
            return static_cast<int>(WKP_STATUS_INVALID_ARGUMENT);
        }

        auto *ctx = static_cast<wkp_context *>(std::malloc(sizeof(wkp_context)));
        if (ctx == nullptr)
        {
            return static_cast<int>(WKP_STATUS_ALLOCATION_FAILED);
        }

        std::memset(ctx, 0, sizeof(wkp_context));
        const wkp_status status = wkp_context_init(ctx);
        if (status != WKP_STATUS_OK)
        {
            std::free(ctx);
            return static_cast<int>(status);
        }

        *out_workspace = ctx;
        return static_cast<int>(WKP_STATUS_OK);
    }

    void wkp_wasm_workspace_destroy(wkp_context *workspace)
    {
        if (workspace == nullptr)
        {
            return;
        }
        wkp_context_free(workspace);
        std::free(workspace);
    }

    int wkp_wasm_workspace_encode_f64(
        wkp_context *workspace,
        const double *values,
        size_t value_count,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        (void)error_message;
        (void)error_message_capacity;
        return static_cast<int>(wkp_encode_f64(
            workspace,
            values,
            value_count,
            dimensions,
            precisions,
            precision_count,
            out_data,
            out_size));
    }

    int wkp_wasm_workspace_decode_f64(
        wkp_context *workspace,
        const uint8_t *encoded,
        size_t encoded_size,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const double **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        (void)error_message;
        (void)error_message_capacity;
        return static_cast<int>(wkp_decode_f64(
            workspace,
            encoded,
            encoded_size,
            dimensions,
            precisions,
            precision_count,
            out_data,
            out_size));
    }

    int wkp_wasm_workspace_encode_geometry_frame_f64(
        wkp_context *workspace,
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
        size_t error_message_capacity)
    {
        (void)error_message;
        (void)error_message_capacity;
        return static_cast<int>(wkp_encode_geometry_frame(
            workspace,
            geometry_type,
            coords,
            coord_value_count,
            dimensions,
            precision,
            group_segment_counts,
            group_count,
            segment_point_counts,
            segment_count,
            out_data,
            out_size));
    }

    int wkp_wasm_workspace_decode_geometry_frame_f64(
        wkp_context *workspace,
        const uint8_t *encoded,
        size_t encoded_size,
        int *out_version,
        int *out_precision,
        int *out_dimensions,
        int *out_geometry_type,
        const double **out_coords,
        size_t *out_coord_value_count,
        const size_t **out_segment_point_counts,
        size_t *out_segment_count,
        const size_t **out_group_segment_counts,
        size_t *out_group_count,
        char *error_message,
        size_t error_message_capacity)
    {
        (void)error_message;
        (void)error_message_capacity;
        if (out_version == nullptr || out_precision == nullptr || out_dimensions == nullptr || out_geometry_type == nullptr ||
            out_coords == nullptr || out_coord_value_count == nullptr ||
            out_segment_point_counts == nullptr || out_segment_count == nullptr ||
            out_group_segment_counts == nullptr || out_group_count == nullptr)
        {
            return static_cast<int>(WKP_STATUS_INVALID_ARGUMENT);
        }

        const wkp_geometry_frame_f64 *frame = nullptr;
        const wkp_status status = wkp_decode_geometry_frame(
            workspace,
            encoded,
            encoded_size,
            &frame);

        if (status != WKP_STATUS_OK)
        {
            return static_cast<int>(status);
        }

        *out_version = frame->version;
        *out_precision = frame->precision;
        *out_dimensions = frame->dimensions;
        *out_geometry_type = frame->geometry_type;
        *out_coords = frame->coords;
        *out_coord_value_count = frame->coord_value_count;
        *out_segment_point_counts = frame->segment_point_counts;
        *out_segment_count = frame->segment_count;
        *out_group_segment_counts = frame->group_segment_counts;
        *out_group_count = frame->group_count;
        return static_cast<int>(WKP_STATUS_OK);
    }

    int wkp_wasm_decode_geometry_header(
        const uint8_t *encoded,
        size_t encoded_size,
        int *out_version,
        int *out_precision,
        int *out_dimensions,
        int *out_geometry_type,
        char *error_message,
        size_t error_message_capacity)
    {
        (void)error_message;
        (void)error_message_capacity;
        return static_cast<int>(wkp_decode_geometry_header(
            encoded,
            encoded_size,
            out_version,
            out_precision,
            out_dimensions,
            out_geometry_type));
    }

    const char *wkp_wasm_core_version()
    {
        return WKP_CORE_VERSION;
    }

    int wkp_wasm_geometry_point() { return WKP_GEOMETRY_POINT; }
    int wkp_wasm_geometry_linestring() { return WKP_GEOMETRY_LINESTRING; }
    int wkp_wasm_geometry_polygon() { return WKP_GEOMETRY_POLYGON; }
    int wkp_wasm_geometry_multipoint() { return WKP_GEOMETRY_MULTIPOINT; }
    int wkp_wasm_geometry_multilinestring() { return WKP_GEOMETRY_MULTILINESTRING; }
    int wkp_wasm_geometry_multipolygon() { return WKP_GEOMETRY_MULTIPOLYGON; }

    int wkp_wasm_status_ok() { return WKP_STATUS_OK; }
    int wkp_wasm_status_invalid_argument() { return WKP_STATUS_INVALID_ARGUMENT; }
    int wkp_wasm_status_malformed_input() { return WKP_STATUS_MALFORMED_INPUT; }
    int wkp_wasm_status_allocation_failed() { return WKP_STATUS_ALLOCATION_FAILED; }
    int wkp_wasm_status_buffer_too_small() { return WKP_STATUS_BUFFER_TOO_SMALL; }
    int wkp_wasm_status_limit_exceeded() { return WKP_STATUS_LIMIT_EXCEEDED; }
    int wkp_wasm_status_internal_error() { return WKP_STATUS_INTERNAL_ERROR; }
}
