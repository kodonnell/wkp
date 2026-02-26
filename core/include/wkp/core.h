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
        WKP_STATUS_INTERNAL_ERROR = 255
    } wkp_status;

    typedef struct wkp_u8_buffer
    {
        uint8_t *data;
        size_t size;
    } wkp_u8_buffer;

    typedef struct wkp_f64_buffer
    {
        double *data;
        size_t size;
    } wkp_f64_buffer;

    wkp_status wkp_encode_f64(
        const double *values,
        size_t value_count,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity);

    wkp_status wkp_decode_f64(
        const uint8_t *encoded,
        size_t encoded_size,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        wkp_f64_buffer *out_values,
        char *error_message,
        size_t error_message_capacity);

    void wkp_free_u8_buffer(wkp_u8_buffer *buffer);
    void wkp_free_f64_buffer(wkp_f64_buffer *buffer);

#ifdef __cplusplus
}
#endif
