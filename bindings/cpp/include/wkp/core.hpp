#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "wkp/core.h"

namespace wkp::core
{
    /* The four header fields of an encoded geometry. geometry_type is a wkp_geometry_type. */
    struct GeometryHeader
    {
        int version = 0;
        int precision = 0;
        int dimensions = 0;
        int geometry_type = 0;
    };

    namespace detail
    {
        inline void throw_for_status(wkp_status status)
        {
            switch (status)
            {
            case WKP_STATUS_OK:
                return;
            case WKP_STATUS_INVALID_ARGUMENT:
                throw std::invalid_argument("WKP: invalid argument");
            case WKP_STATUS_MALFORMED_INPUT:
                throw std::invalid_argument("WKP: malformed input");
            default:
                throw std::runtime_error("WKP: internal error (status " + std::to_string(status) + ")");
            }
        }

        // Owns one thread's context and frees it when the thread exits.
        struct thread_context
        {
            wkp_context *ctx = wkp_context_new();

            thread_context() = default;
            ~thread_context() { wkp_context_free(ctx); }
            thread_context(const thread_context &) = delete;
            thread_context &operator=(const thread_context &) = delete;
        };

        inline wkp_context *thread_ctx()
        {
            thread_local thread_context holder;
            return holder.ctx;
        }
    } // namespace detail

    /* Encodes a headerless float stream: `values` are flat and interleaved, `dimensions` per
       point; `precisions` has one entry (all dimensions) or one per dimension. */
    inline std::string encode_f64(
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        const uint8_t *data = nullptr;
        size_t size = 0;
        detail::throw_for_status(wkp_encode_f64(
            detail::thread_ctx(),
            values,
            value_count,
            dimensions,
            precisions.data(),
            precisions.size(),
            &data,
            &size));
        return std::string(reinterpret_cast<const char *>(data), size);
    }

    /* Decodes a headerless float stream encoded with the same dimensions and precisions. */
    inline std::vector<double> decode_f64(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        const double *values = nullptr;
        size_t size = 0;
        detail::throw_for_status(wkp_decode_f64(
            detail::thread_ctx(),
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &values,
            &size));
        return std::vector<double>(values, values + size);
    }

    /* Decodes only the header of an encoded geometry. */
    inline GeometryHeader decode_geometry_header(std::string_view encoded)
    {
        GeometryHeader h{};
        detail::throw_for_status(wkp_decode_geometry_header(
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            &h.version,
            &h.precision,
            &h.dimensions,
            &h.geometry_type));
        return h;
    }

} // namespace wkp::core
