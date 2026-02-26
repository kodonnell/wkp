#include <cstddef>
#include <cstdint>

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

        wkp_u8_buffer out{nullptr, 0};
        const wkp_status status = wkp_encode_f64(
            values,
            value_count,
            dimensions,
            precisions,
            precision_count,
            &out,
            error_message,
            error_message_capacity);

        if (status != WKP_STATUS_OK)
        {
            *out_data = nullptr;
            *out_size = 0;
            return static_cast<int>(status);
        }

        *out_data = out.data;
        *out_size = out.size;
        return static_cast<int>(status);
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

        wkp_f64_buffer out{nullptr, 0};
        const wkp_status status = wkp_decode_f64(
            encoded,
            encoded_size,
            dimensions,
            precisions,
            precision_count,
            &out,
            error_message,
            error_message_capacity);

        if (status != WKP_STATUS_OK)
        {
            *out_data = nullptr;
            *out_size = 0;
            return static_cast<int>(status);
        }

        *out_data = out.data;
        *out_size = out.size;
        return static_cast<int>(status);
    }

    void wkp_wasm_free_u8(uint8_t *data, size_t size)
    {
        wkp_u8_buffer buf{data, size};
        wkp_free_u8_buffer(&buf);
    }

    void wkp_wasm_free_f64(double *data, size_t size)
    {
        wkp_f64_buffer buf{data, size};
        wkp_free_f64_buffer(&buf);
    }
}
