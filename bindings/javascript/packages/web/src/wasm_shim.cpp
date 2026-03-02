#include <cstddef>
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

        std::size_t capacity = (value_count * 4) + 64;
        uint8_t *buffer = static_cast<uint8_t *>(std::malloc(capacity));
        if (buffer == nullptr)
        {
            return static_cast<int>(WKP_STATUS_ALLOCATION_FAILED);
        }

        while (true)
        {
            wkp_u8_buffer out{buffer, capacity};
            const wkp_status status = wkp_encode_f64_into(
                values,
                value_count,
                dimensions,
                precisions,
                precision_count,
                &out,
                error_message,
                error_message_capacity);

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                capacity = out.size;
                uint8_t *next = static_cast<uint8_t *>(std::realloc(buffer, capacity));
                if (next == nullptr)
                {
                    std::free(buffer);
                    return static_cast<int>(WKP_STATUS_ALLOCATION_FAILED);
                }
                buffer = next;
                continue;
            }

            if (status != WKP_STATUS_OK)
            {
                std::free(buffer);
                *out_data = nullptr;
                *out_size = 0;
                return static_cast<int>(status);
            }

            *out_data = buffer;
            *out_size = out.size;
            return static_cast<int>(status);
        }
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

        std::size_t capacity = (encoded_size > 0 ? encoded_size : 1) * dimensions;
        if (capacity == 0)
        {
            capacity = dimensions > 0 ? dimensions : 1;
        }
        double *buffer = static_cast<double *>(std::malloc(sizeof(double) * capacity));
        if (buffer == nullptr)
        {
            return static_cast<int>(WKP_STATUS_ALLOCATION_FAILED);
        }

        while (true)
        {
            wkp_f64_buffer out{buffer, capacity};
            const wkp_status status = wkp_decode_f64_into(
                encoded,
                encoded_size,
                dimensions,
                precisions,
                precision_count,
                &out,
                error_message,
                error_message_capacity);

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                capacity = out.size;
                double *next = static_cast<double *>(std::realloc(buffer, sizeof(double) * capacity));
                if (next == nullptr)
                {
                    std::free(buffer);
                    return static_cast<int>(WKP_STATUS_ALLOCATION_FAILED);
                }
                buffer = next;
                continue;
            }

            if (status != WKP_STATUS_OK)
            {
                std::free(buffer);
                *out_data = nullptr;
                *out_size = 0;
                return static_cast<int>(status);
            }

            *out_data = buffer;
            *out_size = out.size;
            return static_cast<int>(status);
        }
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
