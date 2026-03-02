#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "wkp/core.h"

namespace wkp::core
{

    std::string_view version() noexcept;

    enum class EncodedGeometryType
    {
        POINT = WKP_GEOMETRY_POINT,
        LINESTRING = WKP_GEOMETRY_LINESTRING,
        POLYGON = WKP_GEOMETRY_POLYGON,
        MULTIPOINT = WKP_GEOMETRY_MULTIPOINT,
        MULTILINESTRING = WKP_GEOMETRY_MULTILINESTRING,
        MULTIPOLYGON = WKP_GEOMETRY_MULTIPOLYGON,
    };

    static_assert(static_cast<int>(EncodedGeometryType::POINT) == WKP_GEOMETRY_POINT, "Geometry enum drift");
    static_assert(static_cast<int>(EncodedGeometryType::LINESTRING) == WKP_GEOMETRY_LINESTRING, "Geometry enum drift");
    static_assert(static_cast<int>(EncodedGeometryType::POLYGON) == WKP_GEOMETRY_POLYGON, "Geometry enum drift");
    static_assert(static_cast<int>(EncodedGeometryType::MULTIPOINT) == WKP_GEOMETRY_MULTIPOINT, "Geometry enum drift");
    static_assert(static_cast<int>(EncodedGeometryType::MULTILINESTRING) == WKP_GEOMETRY_MULTILINESTRING, "Geometry enum drift");
    static_assert(static_cast<int>(EncodedGeometryType::MULTIPOLYGON) == WKP_GEOMETRY_MULTIPOLYGON, "Geometry enum drift");

    struct GeometryHeader
    {
        int version;
        int precision;
        int dimensions;
        int geometry_type;
    };

    struct GeometryFrame
    {
        GeometryHeader header;
        std::vector<std::vector<std::vector<double>>> groups;
    };

    std::vector<int> normalize_precisions(std::size_t dimensions, const std::vector<int> &precisions);

    GeometryHeader decode_geometry_header(std::string_view encoded);
    GeometryFrame decode_geometry_frame(std::string_view encoded);

    class GeometryEncoder
    {
    public:
        GeometryEncoder(int precision, int dimensions, std::size_t initial_capacity = 4096)
            : precision_(precision), dimensions_(dimensions), initial_capacity_(initial_capacity)
        {
            if (dimensions_ <= 0 || dimensions_ > 16)
            {
                throw std::invalid_argument("dimensions must be between 1 and 16");
            }
        }

        int precision() const noexcept { return precision_; }
        int dimensions() const noexcept { return dimensions_; }

        std::string encode_point(const double *coords, std::size_t point_count)
        {
            return call_simple(wkp_encode_point_f64_into, coords, point_count * static_cast<std::size_t>(dimensions_));
        }

        std::string encode_linestring(const double *coords, std::size_t point_count)
        {
            return call_simple(wkp_encode_linestring_f64_into, coords, point_count * static_cast<std::size_t>(dimensions_));
        }

        std::string encode_polygon(
            const std::vector<const double *> &ring_coords,
            const std::vector<std::size_t> &ring_point_counts)
        {
            if (ring_coords.size() != ring_point_counts.size())
            {
                throw std::invalid_argument("ring coordinate/count size mismatch");
            }

            flat_buffer_.clear();
            flat_buffer_.reserve(initial_capacity_ / sizeof(double));
            for (std::size_t i = 0; i < ring_coords.size(); ++i)
            {
                const std::size_t n = ring_point_counts[i] * static_cast<std::size_t>(dimensions_);
                const double *src = ring_coords[i];
                if (src == nullptr)
                {
                    throw std::invalid_argument("ring coords pointer cannot be null");
                }
                const std::size_t old_size = flat_buffer_.size();
                flat_buffer_.resize(old_size + n);
                std::memcpy(flat_buffer_.data() + old_size, src, n * sizeof(double));
            }

            return encode_with_retry([&](wkp_u8_buffer *out, char *error, std::size_t error_size)
                                     { return wkp_encode_polygon_f64_into(
                                           flat_buffer_.data(),
                                           flat_buffer_.size(),
                                           static_cast<std::size_t>(dimensions_),
                                           precision_,
                                           ring_point_counts.data(),
                                           ring_point_counts.size(),
                                           out,
                                           error,
                                           error_size); });
        }

        std::string encode_multipoint(
            const std::vector<const double *> &point_coords,
            const std::vector<std::size_t> &point_counts)
        {
            if (point_coords.size() != point_counts.size())
            {
                throw std::invalid_argument("point coordinate/count size mismatch");
            }

            flat_buffer_.clear();
            flat_buffer_.reserve(point_coords.size() * static_cast<std::size_t>(dimensions_));
            for (std::size_t i = 0; i < point_coords.size(); ++i)
            {
                if (point_counts[i] != 1)
                {
                    throw std::invalid_argument("Each MULTIPOINT part must contain exactly one coordinate");
                }

                const double *src = point_coords[i];
                if (src == nullptr)
                {
                    throw std::invalid_argument("point coords pointer cannot be null");
                }
                const std::size_t old_size = flat_buffer_.size();
                const std::size_t n = static_cast<std::size_t>(dimensions_);
                flat_buffer_.resize(old_size + n);
                std::memcpy(flat_buffer_.data() + old_size, src, n * sizeof(double));
            }

            return encode_with_retry([&](wkp_u8_buffer *out, char *error, std::size_t error_size)
                                     { return wkp_encode_multipoint_f64_into(
                                           flat_buffer_.data(),
                                           flat_buffer_.size(),
                                           static_cast<std::size_t>(dimensions_),
                                           precision_,
                                           point_coords.size(),
                                           out,
                                           error,
                                           error_size); });
        }

        std::string encode_multilinestring(
            const std::vector<const double *> &linestring_coords,
            const std::vector<std::size_t> &linestring_point_counts)
        {
            if (linestring_coords.size() != linestring_point_counts.size())
            {
                throw std::invalid_argument("linestring coordinate/count size mismatch");
            }

            flat_buffer_.clear();
            flat_buffer_.reserve(initial_capacity_ / sizeof(double));
            for (std::size_t i = 0; i < linestring_coords.size(); ++i)
            {
                const std::size_t n = linestring_point_counts[i] * static_cast<std::size_t>(dimensions_);
                const double *src = linestring_coords[i];
                if (src == nullptr)
                {
                    throw std::invalid_argument("linestring coords pointer cannot be null");
                }
                const std::size_t old_size = flat_buffer_.size();
                flat_buffer_.resize(old_size + n);
                std::memcpy(flat_buffer_.data() + old_size, src, n * sizeof(double));
            }

            return encode_with_retry([&](wkp_u8_buffer *out, char *error, std::size_t error_size)
                                     { return wkp_encode_multilinestring_f64_into(
                                           flat_buffer_.data(),
                                           flat_buffer_.size(),
                                           static_cast<std::size_t>(dimensions_),
                                           precision_,
                                           linestring_point_counts.data(),
                                           linestring_point_counts.size(),
                                           out,
                                           error,
                                           error_size); });
        }

        std::string encode_multipolygon(
            const std::vector<std::vector<const double *>> &polygon_ring_coords,
            const std::vector<std::vector<std::size_t>> &polygon_ring_point_counts)
        {
            if (polygon_ring_coords.size() != polygon_ring_point_counts.size())
            {
                throw std::invalid_argument("polygon coordinate/count size mismatch");
            }

            flat_buffer_.clear();
            flat_buffer_.reserve(initial_capacity_ / sizeof(double));
            std::vector<std::size_t> polygon_ring_counts;
            std::vector<std::size_t> ring_point_counts;

            polygon_ring_counts.reserve(polygon_ring_coords.size());
            for (std::size_t poly_idx = 0; poly_idx < polygon_ring_coords.size(); ++poly_idx)
            {
                const auto &rings = polygon_ring_coords[poly_idx];
                const auto &counts = polygon_ring_point_counts[poly_idx];
                if (rings.size() != counts.size())
                {
                    throw std::invalid_argument("polygon ring coordinate/count size mismatch");
                }

                polygon_ring_counts.push_back(rings.size());
                for (std::size_t i = 0; i < rings.size(); ++i)
                {
                    const std::size_t n = counts[i] * static_cast<std::size_t>(dimensions_);
                    const double *src = rings[i];
                    if (src == nullptr)
                    {
                        throw std::invalid_argument("polygon ring coords pointer cannot be null");
                    }
                    const std::size_t old_size = flat_buffer_.size();
                    flat_buffer_.resize(old_size + n);
                    std::memcpy(flat_buffer_.data() + old_size, src, n * sizeof(double));
                    ring_point_counts.push_back(counts[i]);
                }
            }

            return encode_with_retry([&](wkp_u8_buffer *out, char *error, std::size_t error_size)
                                     { return wkp_encode_multipolygon_f64_into(
                                           flat_buffer_.data(),
                                           flat_buffer_.size(),
                                           static_cast<std::size_t>(dimensions_),
                                           precision_,
                                           polygon_ring_counts.data(),
                                           polygon_ring_counts.size(),
                                           ring_point_counts.data(),
                                           ring_point_counts.size(),
                                           out,
                                           error,
                                           error_size); });
        }

    private:
        using EncodeSimpleFn = wkp_status (*)(
            const double *,
            size_t,
            size_t,
            int,
            wkp_u8_buffer *,
            char *,
            size_t);

        std::string call_simple(EncodeSimpleFn fn, const double *coords, std::size_t coord_value_count)
        {
            return encode_with_retry([&](wkp_u8_buffer *out, char *error, std::size_t error_size)
                                     { return fn(
                                           coords,
                                           coord_value_count,
                                           static_cast<std::size_t>(dimensions_),
                                           precision_,
                                           out,
                                           error,
                                           error_size); });
        }

        static std::size_t next_capacity(std::size_t current, std::size_t required)
        {
            std::size_t grown = (current > 0) ? (current * 2) : 64;
            if (grown < required)
            {
                grown = required;
            }
            if (grown == 0)
            {
                grown = 64;
            }
            return grown;
        }

        template <typename EncodeFn>
        std::string encode_with_retry(EncodeFn &&encode_fn)
        {
            if (encoded_buffer_.empty())
            {
                const std::size_t initial_bytes = (initial_capacity_ > 0) ? initial_capacity_ : 64;
                encoded_buffer_.resize(initial_bytes);
            }

            char error[512] = {0};
            wkp_status status = WKP_STATUS_INTERNAL_ERROR;
            std::size_t output_size = 0;

            for (;;)
            {
                wkp_u8_buffer out{encoded_buffer_.data(), encoded_buffer_.size()};
                status = encode_fn(&out, error, sizeof(error));
                output_size = out.size;

                if (status == WKP_STATUS_BUFFER_TOO_SMALL)
                {
                    encoded_buffer_.resize(next_capacity(encoded_buffer_.size(), out.size));
                    continue;
                }
                break;
            }

            return take_encoded_or_throw(status, output_size, error);
        }

        std::string take_encoded_or_throw(wkp_status status, std::size_t output_size, const char *error)
        {
            if (status != WKP_STATUS_OK)
            {
                const std::string msg = (error != nullptr && error[0] != '\0') ? std::string(error) : std::string("WKP error");
                if (status == WKP_STATUS_INVALID_ARGUMENT || status == WKP_STATUS_MALFORMED_INPUT || status == WKP_STATUS_BUFFER_TOO_SMALL)
                {
                    throw std::invalid_argument(msg);
                }
                throw std::runtime_error(msg);
            }

            return std::string(reinterpret_cast<const char *>(encoded_buffer_.data()), output_size);
        }

        int precision_;
        int dimensions_;
        std::size_t initial_capacity_;
        std::vector<uint8_t> encoded_buffer_;
        std::vector<double> flat_buffer_;
    };

    void encode_f64_into(
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<int> &precisions,
        std::string &out_encoded);

    void decode_f64_into(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions,
        std::vector<double> &out_values);

} // namespace wkp::core
