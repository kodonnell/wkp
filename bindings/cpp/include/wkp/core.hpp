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
    struct GeometryHeader
    {
        int version = 0;
        int precision = 0;
        int dimensions = 0;
        int geometry_type = 0;
    };

    inline void throw_for_status(wkp_status status)
    {
        if (status == WKP_STATUS_OK)
            return;
        const std::string msg = "WKP error";
        if (status == WKP_STATUS_INVALID_ARGUMENT || status == WKP_STATUS_MALFORMED_INPUT || status == WKP_STATUS_BUFFER_TOO_SMALL)
        {
            throw std::invalid_argument(msg);
        }
        if (status == WKP_STATUS_LIMIT_EXCEEDED)
        {
            throw std::length_error(msg);
        }
        throw std::runtime_error(msg);
    }

    inline wkp_context &thread_ctx()
    {
        thread_local wkp_context ctx{};
        thread_local bool initialized = false;
        if (!initialized)
        {
            const auto status = wkp_context_init(&ctx);
            if (status != WKP_STATUS_OK)
            {
                throw std::runtime_error("Failed to initialize WKP context");
            }
            initialized = true;
        }
        return ctx;
    }

    inline std::vector<int> normalize_precisions(std::size_t dimensions, const std::vector<int> &precisions)
    {
        if (dimensions == 0 || dimensions > 16)
        {
            throw std::invalid_argument("dimensions must be between 1 and 16");
        }
        if (precisions.empty())
        {
            throw std::invalid_argument("precisions cannot be empty");
        }
        if (precisions.size() == 1)
        {
            return std::vector<int>(dimensions, precisions[0]);
        }
        if (precisions.size() != dimensions)
        {
            throw std::invalid_argument("Expected precisions.size() == dimensions or 1");
        }
        return precisions;
    }

    inline std::string encode_f64(
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        const std::vector<int> p = normalize_precisions(dimensions, precisions);
        auto &ctx = thread_ctx();
        const uint8_t *data = nullptr;
        size_t size = 0;
        const auto status = wkp_encode_f64(
            &ctx,
            values,
            value_count,
            dimensions,
            p.data(),
            p.size(),
            &data,
            &size);
        throw_for_status(status);
        return std::string(reinterpret_cast<const char *>(data), size);
    }

    inline std::vector<double> decode_f64(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        const std::vector<int> p = normalize_precisions(dimensions, precisions);
        auto &ctx = thread_ctx();
        const double *values = nullptr;
        size_t size = 0;
        const auto status = wkp_decode_f64(
            &ctx,
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            dimensions,
            p.data(),
            p.size(),
            &values,
            &size);
        throw_for_status(status);
        return std::vector<double>(values, values + size);
    }

    inline GeometryHeader decode_geometry_header(std::string_view encoded)
    {
        GeometryHeader h{};
        const auto status = wkp_decode_geometry_header(
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            &h.version,
            &h.precision,
            &h.dimensions,
            &h.geometry_type);
        throw_for_status(status);
        return h;
    }

} // namespace wkp::core
