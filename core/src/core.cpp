#include "wkp/core.h"
#include "core_internal.hpp"
#include "wkp/_version.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

    constexpr std::size_t kMaxDimensions = 16;
    constexpr int kGeometryVersion = 1;
    constexpr char kSepRing = ',';
    constexpr char kSepMulti = ';';

    void set_error(char *error_message, std::size_t capacity, const std::string &message)
    {
        if (error_message == nullptr || capacity == 0)
        {
            return;
        }
        std::size_t n = message.size();
        if (n >= capacity)
        {
            n = capacity - 1;
        }
        std::memcpy(error_message, message.data(), n);
        error_message[n] = '\0';
    }

    void append_encoded_signed(std::string &out, long long signed_value)
    {
        unsigned long long value = static_cast<unsigned long long>(signed_value << 1);
        if (signed_value < 0)
        {
            value = static_cast<unsigned long long>(~value);
        }

        while (value >= 0x20ULL)
        {
            out.push_back(static_cast<char>((0x20ULL | (value & 0x1FULL)) + 63ULL));
            value >>= 5ULL;
        }
        out.push_back(static_cast<char>(value + 63ULL));
    }

    void append_encoded_segment(
        std::string &out,
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<double> &factors)
    {
        std::vector<long long> previous(dimensions, 0LL);
        for (std::size_t i = 0; i < value_count; ++i)
        {
            const std::size_t dim = i % dimensions;
            const long long scaled = static_cast<long long>(std::llround(values[i] * factors[dim]));
            const long long delta = scaled - previous[dim];
            previous[dim] = scaled;
            append_encoded_signed(out, delta);
        }
    }

    long long decode_once(std::string_view encoded, std::size_t &index)
    {
        int shift = 0;
        unsigned long long result = 0;

        while (true)
        {
            if (index >= encoded.size())
            {
                throw std::invalid_argument("Malformed encoded input");
            }

            int byte = static_cast<unsigned char>(encoded[index]) - 63;
            ++index;

            if (byte < 0)
            {
                throw std::invalid_argument("Malformed encoded input");
            }

            result |= static_cast<unsigned long long>(byte & 0x1F) << shift;
            shift += 5;

            if (shift > 63)
            {
                throw std::invalid_argument("Encoded value overflow");
            }

            if (byte < 0x20)
            {
                break;
            }
        }

        if ((result & 1ULL) != 0ULL)
        {
            return static_cast<long long>(~(result >> 1ULL));
        }
        return static_cast<long long>(result >> 1ULL);
    }

    std::vector<std::pair<std::size_t, std::size_t>> split_ranges(
        std::string_view encoded,
        std::size_t start,
        std::size_t end,
        char sep)
    {
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        std::size_t seg_start = start;

        for (std::size_t i = start; i < end; ++i)
        {
            if (encoded[i] == sep)
            {
                ranges.emplace_back(seg_start, i);
                seg_start = i + 1;
            }
        }
        ranges.emplace_back(seg_start, end);
        return ranges;
    }

    std::vector<double> build_uniform_factors(std::size_t dimensions, int precision)
    {
        std::vector<double> factors(dimensions, 1.0);
        const double factor = std::pow(10.0, static_cast<double>(precision));
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = factor;
        }
        return factors;
    }

    std::string build_geometry_header(std::size_t dimensions, int precision, wkp_geometry_type geometry_type)
    {
        if (dimensions == 0 || dimensions > kMaxDimensions)
        {
            throw std::invalid_argument("dimensions must be between 1 and 16");
        }
        if (precision < 0 || precision > 99)
        {
            throw std::invalid_argument("precision must be between 0 and 99");
        }

        char buf[16];
        std::snprintf(
            buf,
            sizeof(buf),
            "%02d%02d%02d%02d",
            kGeometryVersion,
            precision,
            static_cast<int>(dimensions),
            static_cast<int>(geometry_type));
        return std::string(buf);
    }

    void append_coords_segment(
        std::string &out,
        const double *coords,
        std::size_t point_count,
        std::size_t dimensions,
        const std::vector<double> &factors)
    {
        if (coords == nullptr)
        {
            throw std::invalid_argument("coords pointer cannot be null");
        }
        append_encoded_segment(out, coords, point_count * dimensions, dimensions, factors);
    }

    std::string encode_geometry_point(
        const double *coords,
        std::size_t coord_value_count,
        std::size_t dimensions,
        int precision)
    {
        const std::size_t point_count = coord_value_count / dimensions;
        if (point_count != 1)
        {
            throw std::invalid_argument("POINT requires exactly 1 coordinate");
        }

        const auto factors = build_uniform_factors(dimensions, precision);
        std::string out = build_geometry_header(dimensions, precision, WKP_GEOMETRY_POINT);
        out.reserve(8 + coord_value_count * 4 + 8);
        append_coords_segment(out, coords, point_count, dimensions, factors);
        return out;
    }

    std::string encode_geometry_linestring(
        const double *coords,
        std::size_t coord_value_count,
        std::size_t dimensions,
        int precision)
    {
        const std::size_t point_count = coord_value_count / dimensions;
        if (point_count < 2)
        {
            throw std::invalid_argument("LINESTRING requires at least 2 coordinates");
        }

        const auto factors = build_uniform_factors(dimensions, precision);
        std::string out = build_geometry_header(dimensions, precision, WKP_GEOMETRY_LINESTRING);
        out.reserve(8 + coord_value_count * 4 + 8);
        append_coords_segment(out, coords, point_count, dimensions, factors);
        return out;
    }

    std::string encode_geometry_polygon(
        const double *coords,
        std::size_t coord_value_count,
        std::size_t dimensions,
        int precision,
        const std::size_t *ring_point_counts,
        std::size_t ring_count)
    {
        if (ring_point_counts == nullptr)
        {
            throw std::invalid_argument("ring_point_counts is required");
        }
        if (ring_count == 0)
        {
            throw std::invalid_argument("POLYGON requires at least one ring");
        }

        std::size_t total_points = 0;
        for (std::size_t i = 0; i < ring_count; ++i)
        {
            total_points += ring_point_counts[i];
        }
        if (total_points * dimensions != coord_value_count)
        {
            throw std::invalid_argument("ring point counts do not match coord_value_count");
        }

        const auto factors = build_uniform_factors(dimensions, precision);
        std::string out = build_geometry_header(dimensions, precision, WKP_GEOMETRY_POLYGON);
        out.reserve(8 + coord_value_count * 4 + ring_count + 8);

        std::size_t offset = 0;
        for (std::size_t i = 0; i < ring_count; ++i)
        {
            if (i > 0)
            {
                out.push_back(kSepRing);
            }
            const std::size_t point_count = ring_point_counts[i];
            append_coords_segment(out, coords + offset, point_count, dimensions, factors);
            offset += point_count * dimensions;
        }

        return out;
    }

    std::string encode_geometry_multipoint(
        const double *coords,
        std::size_t coord_value_count,
        std::size_t dimensions,
        int precision,
        std::size_t point_count)
    {
        if (point_count * dimensions != coord_value_count)
        {
            throw std::invalid_argument("point_count does not match coord_value_count");
        }

        const auto factors = build_uniform_factors(dimensions, precision);
        std::string out = build_geometry_header(dimensions, precision, WKP_GEOMETRY_MULTIPOINT);
        out.reserve(8 + coord_value_count * 4 + point_count + 8);

        std::size_t offset = 0;
        for (std::size_t i = 0; i < point_count; ++i)
        {
            if (i > 0)
            {
                out.push_back(kSepMulti);
            }
            append_coords_segment(out, coords + offset, 1, dimensions, factors);
            offset += dimensions;
        }

        return out;
    }

    std::string encode_geometry_multilinestring(
        const double *coords,
        std::size_t coord_value_count,
        std::size_t dimensions,
        int precision,
        const std::size_t *linestring_point_counts,
        std::size_t linestring_count)
    {
        if (linestring_point_counts == nullptr)
        {
            throw std::invalid_argument("linestring_point_counts is required");
        }

        std::size_t total_points = 0;
        for (std::size_t i = 0; i < linestring_count; ++i)
        {
            if (linestring_point_counts[i] < 2)
            {
                throw std::invalid_argument("Each MULTILINESTRING part must contain at least two coordinates");
            }
            total_points += linestring_point_counts[i];
        }
        if (total_points * dimensions != coord_value_count)
        {
            throw std::invalid_argument("linestring point counts do not match coord_value_count");
        }

        const auto factors = build_uniform_factors(dimensions, precision);
        std::string out = build_geometry_header(dimensions, precision, WKP_GEOMETRY_MULTILINESTRING);
        out.reserve(8 + coord_value_count * 4 + linestring_count + 8);

        std::size_t offset = 0;
        for (std::size_t i = 0; i < linestring_count; ++i)
        {
            if (i > 0)
            {
                out.push_back(kSepMulti);
            }
            const std::size_t point_count = linestring_point_counts[i];
            append_coords_segment(out, coords + offset, point_count, dimensions, factors);
            offset += point_count * dimensions;
        }

        return out;
    }

    std::string encode_geometry_multipolygon(
        const double *coords,
        std::size_t coord_value_count,
        std::size_t dimensions,
        int precision,
        const std::size_t *polygon_ring_counts,
        std::size_t polygon_count,
        const std::size_t *ring_point_counts,
        std::size_t ring_count)
    {
        if (polygon_ring_counts == nullptr || ring_point_counts == nullptr)
        {
            throw std::invalid_argument("polygon_ring_counts and ring_point_counts are required");
        }

        std::size_t ring_total = 0;
        for (std::size_t i = 0; i < polygon_count; ++i)
        {
            ring_total += polygon_ring_counts[i];
        }
        if (ring_total != ring_count)
        {
            throw std::invalid_argument("polygon_ring_counts must sum to ring_count");
        }

        std::size_t total_points = 0;
        for (std::size_t i = 0; i < ring_count; ++i)
        {
            total_points += ring_point_counts[i];
        }
        if (total_points * dimensions != coord_value_count)
        {
            throw std::invalid_argument("ring_point_counts do not match coord_value_count");
        }

        const auto factors = build_uniform_factors(dimensions, precision);
        std::string out = build_geometry_header(dimensions, precision, WKP_GEOMETRY_MULTIPOLYGON);
        out.reserve(8 + coord_value_count * 4 + ring_count + polygon_count + 8);

        std::size_t coord_offset = 0;
        std::size_t ring_offset = 0;
        for (std::size_t poly_idx = 0; poly_idx < polygon_count; ++poly_idx)
        {
            if (poly_idx > 0)
            {
                out.push_back(kSepMulti);
            }

            const std::size_t poly_ring_count = polygon_ring_counts[poly_idx];
            if (poly_ring_count == 0)
            {
                throw std::invalid_argument("Each MULTIPOLYGON part requires at least one ring");
            }

            for (std::size_t ring_idx = 0; ring_idx < poly_ring_count; ++ring_idx)
            {
                if (ring_idx > 0)
                {
                    out.push_back(kSepRing);
                }

                const std::size_t point_count = ring_point_counts[ring_offset++];
                append_coords_segment(out, coords + coord_offset, point_count, dimensions, factors);
                coord_offset += point_count * dimensions;
            }
        }

        return out;
    }

} // namespace

namespace wkp::core
{

    std::string_view version() noexcept
    {
        return WKP_CORE_VERSION;
    }

    std::vector<int> normalize_precisions(std::size_t dimensions, const std::vector<int> &precisions)
    {
        if (dimensions == 0 || dimensions > kMaxDimensions)
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

    GeometryHeader decode_geometry_header(std::string_view encoded)
    {
        if (encoded.size() < 8)
        {
            throw std::invalid_argument("Encoded geometry header too short");
        }

        const int version = std::stoi(std::string(encoded.substr(0, 2)));
        if (version != kGeometryVersion)
        {
            throw std::invalid_argument("Unsupported geometry encoding version");
        }

        return GeometryHeader{
            version,
            std::stoi(std::string(encoded.substr(2, 2))),
            std::stoi(std::string(encoded.substr(4, 2))),
            std::stoi(std::string(encoded.substr(6, 2))),
        };
    }

    GeometryFrame decode_geometry_frame(std::string_view encoded)
    {
        const GeometryHeader header = decode_geometry_header(encoded);
        const std::size_t body_start = 8;
        const std::size_t body_end = encoded.size();
        const std::size_t dims = static_cast<std::size_t>(header.dimensions);
        const std::vector<int> precisions{header.precision};

        auto decode_segment = [&](std::size_t start, std::size_t end) -> std::vector<double>
        {
            if (end < start || end == start)
            {
                throw std::invalid_argument("Malformed encoded geometry segment");
            }
            return decode_f64(encoded.substr(start, end - start), dims, precisions);
        };

        GeometryFrame frame{header, {}};

        switch (static_cast<EncodedGeometryType>(header.geometry_type))
        {
        case EncodedGeometryType::POINT:
        case EncodedGeometryType::LINESTRING:
        {
            frame.groups.push_back({decode_segment(body_start, body_end)});
            break;
        }

        case EncodedGeometryType::POLYGON:
        {
            std::vector<std::vector<double>> rings;
            for (const auto &r : split_ranges(encoded, body_start, body_end, kSepRing))
            {
                rings.push_back(decode_segment(r.first, r.second));
            }
            frame.groups.push_back(std::move(rings));
            break;
        }

        case EncodedGeometryType::MULTIPOINT:
        case EncodedGeometryType::MULTILINESTRING:
        {
            for (const auto &r : split_ranges(encoded, body_start, body_end, kSepMulti))
            {
                frame.groups.push_back({decode_segment(r.first, r.second)});
            }
            break;
        }

        case EncodedGeometryType::MULTIPOLYGON:
        {
            for (const auto &poly_range : split_ranges(encoded, body_start, body_end, kSepMulti))
            {
                std::vector<std::vector<double>> rings;
                for (const auto &ring_range : split_ranges(encoded, poly_range.first, poly_range.second, kSepRing))
                {
                    rings.push_back(decode_segment(ring_range.first, ring_range.second));
                }
                frame.groups.push_back(std::move(rings));
            }
            break;
        }

        default:
            throw std::invalid_argument("Unsupported geometry type in header");
        }

        return frame;
    }

    std::string encode_f64(
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        const std::vector<int> p = normalize_precisions(dimensions, precisions);
        if (values == nullptr)
        {
            throw std::invalid_argument("values pointer cannot be null");
        }
        if (value_count % dimensions != 0)
        {
            throw std::invalid_argument("value_count must be divisible by dimensions");
        }

        std::vector<double> factors(dimensions, 1.0);
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = std::pow(10.0, static_cast<double>(p[i]));
        }

        std::string out;
        out.reserve(value_count * 4 + 16);

        append_encoded_segment(out, values, value_count, dimensions, factors);

        return out;
    }

    std::string encode_f64(
        const std::vector<double> &values,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        return encode_f64(values.data(), values.size(), dimensions, precisions);
    }

    std::vector<double> decode_f64(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        const std::vector<int> p = normalize_precisions(dimensions, precisions);

        std::vector<double> factors(dimensions, 1.0);
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = std::pow(10.0, static_cast<double>(p[i]));
        }

        std::vector<long long> previous(dimensions, 0LL);
        std::vector<double> out;
        out.reserve(encoded.size() / 2 + 1);

        std::size_t index = 0;
        std::size_t value_count = 0;
        while (index < encoded.size())
        {
            const std::size_t dim = value_count % dimensions;
            previous[dim] += decode_once(encoded, index);
            out.push_back(static_cast<double>(previous[dim]) / factors[dim]);
            ++value_count;
        }

        if ((value_count % dimensions) != 0U)
        {
            throw std::invalid_argument("Malformed encoded coordinate stream");
        }

        return out;
    }

} // namespace wkp::core

namespace
{

    wkp_status encode_string_result(
        const std::string &encoded,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        auto *data = static_cast<uint8_t *>(::operator new(encoded.size(), std::nothrow));
        if (data == nullptr)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }

        std::memcpy(data, encoded.data(), encoded.size());
        out_encoded->data = data;
        out_encoded->size = encoded.size();
        return WKP_STATUS_OK;
    }

    bool validate_geometry_inputs(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (out_encoded == nullptr)
        {
            set_error(error_message, error_message_capacity, "out_encoded is required");
            return false;
        }

        out_encoded->data = nullptr;
        out_encoded->size = 0;

        if (coords == nullptr)
        {
            set_error(error_message, error_message_capacity, "coords is required");
            return false;
        }

        if (dimensions == 0)
        {
            set_error(error_message, error_message_capacity, "dimensions must be greater than 0");
            return false;
        }

        if ((coord_value_count % dimensions) != 0)
        {
            set_error(error_message, error_message_capacity, "coord_value_count must be divisible by dimensions");
            return false;
        }

        return true;
    }

} // namespace

extern "C"
{

    wkp_status wkp_encode_f64(
        const double *values,
        size_t value_count,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (out_encoded == nullptr)
        {
            set_error(error_message, error_message_capacity, "out_encoded is required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        out_encoded->data = nullptr;
        out_encoded->size = 0;

        if (values == nullptr || precisions == nullptr)
        {
            set_error(error_message, error_message_capacity, "values and precisions are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        try
        {
            std::vector<int> p(precisions, precisions + precision_count);
            std::string encoded = wkp::core::encode_f64(values, value_count, dimensions, p);

            auto *data = static_cast<uint8_t *>(::operator new(encoded.size(), std::nothrow));
            if (data == nullptr)
            {
                set_error(error_message, error_message_capacity, "Allocation failed");
                return WKP_STATUS_ALLOCATION_FAILED;
            }
            std::memcpy(data, encoded.data(), encoded.size());

            out_encoded->data = data;
            out_encoded->size = encoded.size();
            return WKP_STATUS_OK;
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_decode_f64(
        const uint8_t *encoded,
        size_t encoded_size,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        wkp_f64_buffer *out_values,
        char *error_message,
        size_t error_message_capacity)
    {
        if (out_values == nullptr)
        {
            set_error(error_message, error_message_capacity, "out_values is required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        out_values->data = nullptr;
        out_values->size = 0;

        if (encoded == nullptr || precisions == nullptr)
        {
            set_error(error_message, error_message_capacity, "encoded and precisions are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        try
        {
            std::vector<int> p(precisions, precisions + precision_count);
            const std::string_view encoded_view(
                reinterpret_cast<const char *>(encoded),
                encoded_size);
            std::vector<double> decoded = wkp::core::decode_f64(encoded_view, dimensions, p);

            auto *data = static_cast<double *>(::operator new(sizeof(double) * decoded.size(), std::nothrow));
            if (data == nullptr)
            {
                set_error(error_message, error_message_capacity, "Allocation failed");
                return WKP_STATUS_ALLOCATION_FAILED;
            }
            std::memcpy(data, decoded.data(), sizeof(double) * decoded.size());

            out_values->data = data;
            out_values->size = decoded.size();
            return WKP_STATUS_OK;
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            const std::string message = ex.what();
            if (message.find("Malformed") != std::string::npos)
            {
                set_error(error_message, error_message_capacity, message);
                return WKP_STATUS_MALFORMED_INPUT;
            }
            set_error(error_message, error_message_capacity, message);
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_encode_point_f64(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_geometry_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        try
        {
            const std::string encoded = encode_geometry_point(coords, coord_value_count, dimensions, precision);
            return encode_string_result(encoded, out_encoded, error_message, error_message_capacity);
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_encode_linestring_f64(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_geometry_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        try
        {
            const std::string encoded = encode_geometry_linestring(coords, coord_value_count, dimensions, precision);
            return encode_string_result(encoded, out_encoded, error_message, error_message_capacity);
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_encode_polygon_f64(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *ring_point_counts,
        size_t ring_count,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_geometry_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            const std::string encoded = encode_geometry_polygon(
                coords,
                coord_value_count,
                dimensions,
                precision,
                ring_point_counts,
                ring_count);
            return encode_string_result(encoded, out_encoded, error_message, error_message_capacity);
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_encode_multipoint_f64(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        size_t point_count,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_geometry_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        try
        {
            const std::string encoded = encode_geometry_multipoint(
                coords,
                coord_value_count,
                dimensions,
                precision,
                point_count);
            return encode_string_result(encoded, out_encoded, error_message, error_message_capacity);
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_encode_multilinestring_f64(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *linestring_point_counts,
        size_t linestring_count,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_geometry_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            const std::string encoded = encode_geometry_multilinestring(
                coords,
                coord_value_count,
                dimensions,
                precision,
                linestring_point_counts,
                linestring_count);
            return encode_string_result(encoded, out_encoded, error_message, error_message_capacity);
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_encode_multipolygon_f64(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *polygon_ring_counts,
        size_t polygon_count,
        const size_t *ring_point_counts,
        size_t ring_count,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_geometry_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            const std::string encoded = encode_geometry_multipolygon(
                coords,
                coord_value_count,
                dimensions,
                precision,
                polygon_ring_counts,
                polygon_count,
                ring_point_counts,
                ring_count);
            return encode_string_result(encoded, out_encoded, error_message, error_message_capacity);
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "Allocation failed");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    void wkp_free_u8_buffer(wkp_u8_buffer *buffer)
    {
        if (buffer == nullptr || buffer->data == nullptr)
        {
            return;
        }
        ::operator delete(buffer->data);
        buffer->data = nullptr;
        buffer->size = 0;
    }

    void wkp_free_f64_buffer(wkp_f64_buffer *buffer)
    {
        if (buffer == nullptr || buffer->data == nullptr)
        {
            return;
        }
        ::operator delete(buffer->data);
        buffer->data = nullptr;
        buffer->size = 0;
    }

} // extern "C"
