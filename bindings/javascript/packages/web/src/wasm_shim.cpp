#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#include "wkp/_version.h"
#include "wkp/core.h"

extern "C"
{

    int wkp_wasm_encode_f64(
        const double *values,
        size_t value_count,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        if (out_data == nullptr || out_size == nullptr)
        {
            return static_cast<int>(WKP_STATUS_INVALID_ARGUMENT);
        }

        char local_error[256] = {0};
        char *err = error_message != nullptr ? error_message : local_error;
        const size_t err_cap = error_message != nullptr ? error_message_capacity : sizeof(local_error);

        wkp_workspace *workspace = nullptr;
        wkp_status status = wkp_workspace_create(4096, 256, -1, -1, &workspace, err, err_cap);
        if (status != WKP_STATUS_OK)
        {
            return static_cast<int>(status);
        }

        const uint8_t *workspace_data = nullptr;
        std::size_t workspace_size = 0;
        status = wkp_workspace_encode_f64(
            workspace,
            values,
            value_count,
            dimensions,
            precisions,
            precision_count,
            &workspace_data,
            &workspace_size,
            err,
            err_cap);

        if (status != WKP_STATUS_OK)
        {
            wkp_workspace_destroy(workspace);
            *out_data = nullptr;
            *out_size = 0;
            return static_cast<int>(status);
        }

        uint8_t *buffer = nullptr;
        if (workspace_size > 0)
        {
            buffer = static_cast<uint8_t *>(std::malloc(workspace_size));
            if (buffer == nullptr)
            {
                wkp_workspace_destroy(workspace);
                *out_data = nullptr;
                *out_size = 0;
                return static_cast<int>(WKP_STATUS_ALLOCATION_FAILED);
            }
            std::memcpy(buffer, workspace_data, workspace_size);
        }

        wkp_workspace_destroy(workspace);
        *out_data = buffer;
        *out_size = workspace_size;
        return static_cast<int>(WKP_STATUS_OK);
    }

    int wkp_wasm_decode_f64(
        const uint8_t *encoded,
        size_t encoded_size,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        double **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        if (out_data == nullptr || out_size == nullptr)
        {
            return static_cast<int>(WKP_STATUS_INVALID_ARGUMENT);
        }

        char local_error[256] = {0};
        char *err = error_message != nullptr ? error_message : local_error;
        const size_t err_cap = error_message != nullptr ? error_message_capacity : sizeof(local_error);

        wkp_workspace *workspace = nullptr;
        wkp_status status = wkp_workspace_create(4096, 256, -1, -1, &workspace, err, err_cap);
        if (status != WKP_STATUS_OK)
        {
            return static_cast<int>(status);
        }

        const double *workspace_data = nullptr;
        std::size_t workspace_size = 0;
        status = wkp_workspace_decode_f64(
            workspace,
            encoded,
            encoded_size,
            dimensions,
            precisions,
            precision_count,
            &workspace_data,
            &workspace_size,
            err,
            err_cap);

        if (status != WKP_STATUS_OK)
        {
            wkp_workspace_destroy(workspace);
            *out_data = nullptr;
            *out_size = 0;
            return static_cast<int>(status);
        }

        double *buffer = nullptr;
        if (workspace_size > 0)
        {
            buffer = static_cast<double *>(std::malloc(sizeof(double) * workspace_size));
            if (buffer == nullptr)
            {
                wkp_workspace_destroy(workspace);
                *out_data = nullptr;
                *out_size = 0;
                return static_cast<int>(WKP_STATUS_ALLOCATION_FAILED);
            }
            std::memcpy(buffer, workspace_data, sizeof(double) * workspace_size);
        }

        wkp_workspace_destroy(workspace);
        *out_data = buffer;
        *out_size = workspace_size;
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
        return static_cast<int>(wkp_decode_geometry_header(
            encoded,
            encoded_size,
            out_version,
            out_precision,
            out_dimensions,
            out_geometry_type,
            error_message,
            error_message_capacity));
    }

    void wkp_wasm_free_u8(uint8_t *data, size_t size)
    {
        (void)size;
        std::free(data);
    }

    void wkp_wasm_free_f64(double *data, size_t size)
    {
        (void)size;
        std::free(data);
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
}
