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

struct wkp_workspace
{
    std::vector<uint8_t> u8_work;
    std::vector<double> f64_work;
    std::size_t max_u8_capacity = 0;
    std::size_t max_f64_capacity = 0;
    bool has_u8_limit = false;
    bool has_f64_limit = false;
};

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
            std::vector<double> decoded;
            decode_f64_into(encoded.substr(start, end - start), dims, precisions, decoded);
            return decoded;
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

    void encode_f64_into(
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<int> &precisions,
        std::string &out_encoded)
    {
        const std::vector<int> p = normalize_precisions(dimensions, precisions);
        if (values == nullptr)
        {
            throw std::invalid_argument("values pointer cannot be null");
        }
        if ((value_count % dimensions) != 0)
        {
            throw std::invalid_argument("value_count must be divisible by dimensions");
        }

        const std::size_t min_capacity = 64;
        if (out_encoded.capacity() < min_capacity)
        {
            out_encoded.reserve(min_capacity);
        }

        char error[512] = {0};
        for (;;)
        {
            const std::size_t capacity = out_encoded.capacity();
            out_encoded.resize(capacity);

            wkp_u8_buffer out{
                reinterpret_cast<uint8_t *>(&out_encoded[0]),
                out_encoded.size()};

            const wkp_status status = wkp_encode_f64_into(
                values,
                value_count,
                dimensions,
                p.data(),
                p.size(),
                &out,
                error,
                sizeof(error));

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                std::size_t next_capacity = capacity * 2;
                if (next_capacity < out.size)
                {
                    next_capacity = out.size;
                }
                if (next_capacity < min_capacity)
                {
                    next_capacity = min_capacity;
                }
                out_encoded.reserve(next_capacity);
                continue;
            }

            if (status != WKP_STATUS_OK)
            {
                out_encoded.clear();
                const std::string msg = (error[0] != '\0') ? std::string(error) : std::string("WKP error");
                if (status == WKP_STATUS_INVALID_ARGUMENT || status == WKP_STATUS_MALFORMED_INPUT)
                {
                    throw std::invalid_argument(msg);
                }
                throw std::runtime_error(msg);
            }

            out_encoded.resize(out.size);
            return;
        }
    }

    void decode_f64_into(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions,
        std::vector<double> &out_values)
    {
        const std::vector<int> p = normalize_precisions(dimensions, precisions);

        const std::size_t min_capacity = 16;
        if (out_values.capacity() < min_capacity)
        {
            out_values.reserve(min_capacity);
        }

        char error[512] = {0};
        for (;;)
        {
            const std::size_t capacity = out_values.capacity();
            out_values.resize(capacity);

            wkp_f64_buffer out{out_values.data(), out_values.size()};
            const wkp_status status = wkp_decode_f64_into(
                reinterpret_cast<const uint8_t *>(encoded.data()),
                encoded.size(),
                dimensions,
                p.data(),
                p.size(),
                &out,
                error,
                sizeof(error));

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                std::size_t next_capacity = capacity * 2;
                if (next_capacity < out.size)
                {
                    next_capacity = out.size;
                }
                if (next_capacity < min_capacity)
                {
                    next_capacity = min_capacity;
                }
                out_values.reserve(next_capacity);
                continue;
            }

            if (status != WKP_STATUS_OK)
            {
                out_values.clear();
                const std::string msg = (error[0] != '\0') ? std::string(error) : std::string("WKP error");
                if (status == WKP_STATUS_INVALID_ARGUMENT || status == WKP_STATUS_MALFORMED_INPUT)
                {
                    throw std::invalid_argument(msg);
                }
                throw std::runtime_error(msg);
            }

            out_values.resize(out.size);
            return;
        }
    }

} // namespace wkp::core

namespace
{
    struct OutputWriter
    {
        uint8_t *data = nullptr;
        std::size_t capacity = 0;
        std::size_t written = 0;
        bool overflow = false;

        void push(char c)
        {
            if (written < capacity && data != nullptr)
            {
                data[written] = static_cast<uint8_t>(c);
            }
            else
            {
                overflow = true;
            }
            ++written;
        }
    };

    bool normalize_max_capacity(
        int64_t raw_max,
        std::size_t *out_limit,
        bool *out_has_limit,
        char *error_message,
        std::size_t error_message_capacity)
    {
        if (raw_max == -1)
        {
            *out_limit = 0;
            *out_has_limit = false;
            return true;
        }
        if (raw_max < 0)
        {
            set_error(error_message, error_message_capacity, "max capacity must be -1 (unlimited) or >= 0");
            return false;
        }

        *out_limit = static_cast<std::size_t>(raw_max);
        *out_has_limit = true;
        return true;
    }

    wkp_status resize_workspace_u8(
        wkp_workspace *workspace,
        std::size_t required,
        char *error_message,
        std::size_t error_message_capacity)
    {
        if (workspace->has_u8_limit && required > workspace->max_u8_capacity)
        {
            set_error(error_message, error_message_capacity, "workspace u8 buffer exceeded max_size");
            return WKP_STATUS_LIMIT_EXCEEDED;
        }
        try
        {
            workspace->u8_work.resize(required);
            return WKP_STATUS_OK;
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "failed to resize workspace u8 buffer");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
    }

    wkp_status resize_workspace_f64(
        wkp_workspace *workspace,
        std::size_t required,
        char *error_message,
        std::size_t error_message_capacity)
    {
        if (workspace->has_f64_limit && required > workspace->max_f64_capacity)
        {
            set_error(error_message, error_message_capacity, "workspace f64 buffer exceeded max_size");
            return WKP_STATUS_LIMIT_EXCEEDED;
        }
        try
        {
            workspace->f64_work.resize(required);
            return WKP_STATUS_OK;
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "failed to resize workspace f64 buffer");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
    }

    template <typename EncodeFn>
    wkp_status workspace_encode_u8_retry(
        wkp_workspace *workspace,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity,
        EncodeFn &&encode_fn)
    {
        if (workspace == nullptr || out_data == nullptr || out_size == nullptr)
        {
            set_error(error_message, error_message_capacity, "workspace, out_data, and out_size are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        if (workspace->u8_work.empty())
        {
            constexpr std::size_t kDefaultCapacity = 64;
            const wkp_status reserve_status = resize_workspace_u8(
                workspace,
                kDefaultCapacity,
                error_message,
                error_message_capacity);
            if (reserve_status != WKP_STATUS_OK)
            {
                return reserve_status;
            }
        }

        for (;;)
        {
            wkp_u8_buffer out{
                workspace->u8_work.data(),
                workspace->u8_work.size()};

            const wkp_status status = encode_fn(
                &out,
                error_message,
                error_message_capacity);

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                const wkp_status reserve_status = resize_workspace_u8(
                    workspace,
                    out.size,
                    error_message,
                    error_message_capacity);
                if (reserve_status != WKP_STATUS_OK)
                {
                    return reserve_status;
                }
                continue;
            }

            if (status != WKP_STATUS_OK)
            {
                return status;
            }

            *out_data = workspace->u8_work.data();
            *out_size = out.size;
            return WKP_STATUS_OK;
        }
    }

    bool normalize_precisions_fixed(
        std::size_t dimensions,
        const int *precisions,
        std::size_t precision_count,
        int *out_precisions,
        char *error_message,
        std::size_t error_message_capacity)
    {
        if (dimensions == 0 || dimensions > kMaxDimensions)
        {
            set_error(error_message, error_message_capacity, "dimensions must be between 1 and 16");
            return false;
        }
        if (precisions == nullptr || precision_count == 0)
        {
            set_error(error_message, error_message_capacity, "precisions cannot be empty");
            return false;
        }

        if (precision_count == 1)
        {
            for (std::size_t i = 0; i < dimensions; ++i)
            {
                out_precisions[i] = precisions[0];
            }
            return true;
        }

        if (precision_count != dimensions)
        {
            set_error(error_message, error_message_capacity, "Expected precisions.size() == dimensions or 1");
            return false;
        }

        for (std::size_t i = 0; i < dimensions; ++i)
        {
            out_precisions[i] = precisions[i];
        }
        return true;
    }

    void append_encoded_signed(OutputWriter &out, long long signed_value)
    {
        unsigned long long value = static_cast<unsigned long long>(signed_value << 1);
        if (signed_value < 0)
        {
            value = static_cast<unsigned long long>(~value);
        }

        while (value >= 0x20ULL)
        {
            out.push(static_cast<char>((0x20ULL | (value & 0x1FULL)) + 63ULL));
            value >>= 5ULL;
        }
        out.push(static_cast<char>(value + 63ULL));
    }

    void append_encoded_segment(
        OutputWriter &out,
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const double *factors)
    {
        long long previous[kMaxDimensions] = {0};
        for (std::size_t i = 0; i < value_count; ++i)
        {
            const std::size_t dim = i % dimensions;
            const long long scaled = static_cast<long long>(std::llround(values[i] * factors[dim]));
            const long long delta = scaled - previous[dim];
            previous[dim] = scaled;
            append_encoded_signed(out, delta);
        }
    }

    bool write_geometry_header(
        OutputWriter &out,
        std::size_t dimensions,
        int precision,
        wkp_geometry_type geometry_type,
        char *error_message,
        std::size_t error_message_capacity)
    {
        if (dimensions == 0 || dimensions > kMaxDimensions)
        {
            set_error(error_message, error_message_capacity, "dimensions must be between 1 and 16");
            return false;
        }
        if (precision < 0 || precision > 99)
        {
            set_error(error_message, error_message_capacity, "precision must be between 0 and 99");
            return false;
        }

        char header[16];
        std::snprintf(
            header,
            sizeof(header),
            "%02d%02d%02d%02d",
            kGeometryVersion,
            precision,
            static_cast<int>(dimensions),
            static_cast<int>(geometry_type));
        for (int i = 0; i < 8; ++i)
        {
            out.push(header[i]);
        }
        return true;
    }

    void init_uniform_factors(std::size_t dimensions, int precision, double *factors)
    {
        const double factor = std::pow(10.0, static_cast<double>(precision));
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = factor;
        }
    }

    wkp_status finalize_encoded_result(
        OutputWriter &writer,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        std::size_t error_message_capacity)
    {
        out_encoded->size = writer.written;
        if (writer.overflow)
        {
            set_error(error_message, error_message_capacity, "output buffer too small");
            return WKP_STATUS_BUFFER_TOO_SMALL;
        }
        return WKP_STATUS_OK;
    }

    bool validate_encode_inputs(
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
        if (coords == nullptr)
        {
            set_error(error_message, error_message_capacity, "coords is required");
            return false;
        }
        if (dimensions == 0 || dimensions > kMaxDimensions)
        {
            set_error(error_message, error_message_capacity, "dimensions must be between 1 and 16");
            return false;
        }
        if ((coord_value_count % dimensions) != 0)
        {
            set_error(error_message, error_message_capacity, "coord_value_count must be divisible by dimensions");
            return false;
        }
        if (out_encoded->size > 0 && out_encoded->data == nullptr)
        {
            set_error(error_message, error_message_capacity, "out_encoded->data is null but size > 0");
            return false;
        }
        return true;
    }

} // namespace

extern "C"
{

    wkp_status wkp_encode_f64_into(
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
        if (values == nullptr || precisions == nullptr)
        {
            set_error(error_message, error_message_capacity, "values and precisions are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (dimensions == 0 || dimensions > kMaxDimensions)
        {
            set_error(error_message, error_message_capacity, "dimensions must be between 1 and 16");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if ((value_count % dimensions) != 0)
        {
            set_error(error_message, error_message_capacity, "value_count must be divisible by dimensions");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (out_encoded->size > 0 && out_encoded->data == nullptr)
        {
            set_error(error_message, error_message_capacity, "out_encoded->data is null but size > 0");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        int p[kMaxDimensions] = {0};
        if (!normalize_precisions_fixed(dimensions, precisions, precision_count, p, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = std::pow(10.0, static_cast<double>(p[i]));
        }

        OutputWriter writer{out_encoded->data, out_encoded->size, 0, false};
        append_encoded_segment(writer, values, value_count, dimensions, factors);
        return finalize_encoded_result(writer, out_encoded, error_message, error_message_capacity);
    }

    wkp_status wkp_decode_f64_into(
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
        if (encoded == nullptr || precisions == nullptr)
        {
            set_error(error_message, error_message_capacity, "encoded and precisions are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (dimensions == 0 || dimensions > kMaxDimensions)
        {
            set_error(error_message, error_message_capacity, "dimensions must be between 1 and 16");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (out_values->size > 0 && out_values->data == nullptr)
        {
            set_error(error_message, error_message_capacity, "out_values->data is null but size > 0");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        int p[kMaxDimensions] = {0};
        if (!normalize_precisions_fixed(dimensions, precisions, precision_count, p, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = std::pow(10.0, static_cast<double>(p[i]));
        }

        const std::string_view encoded_view(reinterpret_cast<const char *>(encoded), encoded_size);
        long long previous[kMaxDimensions] = {0};
        std::size_t value_count = 0;

        try
        {
            std::size_t index = 0;
            while (index < encoded_view.size())
            {
                const std::size_t dim = value_count % dimensions;
                previous[dim] += decode_once(encoded_view, index);
                if (value_count < out_values->size && out_values->data != nullptr)
                {
                    out_values->data[value_count] = static_cast<double>(previous[dim]) / factors[dim];
                }
                ++value_count;
            }
        }
        catch (const std::invalid_argument &ex)
        {
            const std::string message = ex.what();
            set_error(error_message, error_message_capacity, message);
            return message.find("Malformed") != std::string::npos
                       ? WKP_STATUS_MALFORMED_INPUT
                       : WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }

        if ((value_count % dimensions) != 0U)
        {
            set_error(error_message, error_message_capacity, "Malformed encoded coordinate stream");
            return WKP_STATUS_MALFORMED_INPUT;
        }

        if (value_count > out_values->size)
        {
            out_values->size = value_count;
            set_error(error_message, error_message_capacity, "output buffer too small");
            return WKP_STATUS_BUFFER_TOO_SMALL;
        }

        out_values->size = value_count;
        return WKP_STATUS_OK;
    }

    wkp_status wkp_workspace_create(
        size_t initial_u8_capacity,
        size_t initial_f64_capacity,
        int64_t max_u8_capacity,
        int64_t max_f64_capacity,
        wkp_workspace **out_workspace,
        char *error_message,
        size_t error_message_capacity)
    {
        if (out_workspace == nullptr)
        {
            set_error(error_message, error_message_capacity, "out_workspace is required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t u8_limit = 0;
        bool has_u8_limit = false;
        if (!normalize_max_capacity(max_u8_capacity, &u8_limit, &has_u8_limit, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t f64_limit = 0;
        bool has_f64_limit = false;
        if (!normalize_max_capacity(max_f64_capacity, &f64_limit, &has_f64_limit, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        if (has_u8_limit && initial_u8_capacity > u8_limit)
        {
            set_error(error_message, error_message_capacity, "initial u8 capacity exceeds max_size");
            return WKP_STATUS_LIMIT_EXCEEDED;
        }
        if (has_f64_limit && initial_f64_capacity > f64_limit)
        {
            set_error(error_message, error_message_capacity, "initial f64 capacity exceeds max_size");
            return WKP_STATUS_LIMIT_EXCEEDED;
        }

        auto *workspace = new (std::nothrow) wkp_workspace();
        if (workspace == nullptr)
        {
            set_error(error_message, error_message_capacity, "failed to create workspace");
            return WKP_STATUS_ALLOCATION_FAILED;
        }

        workspace->max_u8_capacity = u8_limit;
        workspace->max_f64_capacity = f64_limit;
        workspace->has_u8_limit = has_u8_limit;
        workspace->has_f64_limit = has_f64_limit;

        try
        {
            workspace->u8_work.resize(initial_u8_capacity);
            workspace->f64_work.resize(initial_f64_capacity);
        }
        catch (const std::bad_alloc &)
        {
            delete workspace;
            set_error(error_message, error_message_capacity, "failed to initialize workspace buffers");
            return WKP_STATUS_ALLOCATION_FAILED;
        }

        *out_workspace = workspace;
        return WKP_STATUS_OK;
    }

    void wkp_workspace_destroy(wkp_workspace *workspace)
    {
        delete workspace;
    }

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
        size_t error_message_capacity)
    {
        return workspace_encode_u8_retry(
            workspace,
            out_data,
            out_size,
            error_message,
            error_message_capacity,
            [&](wkp_u8_buffer *out, char *err, size_t err_cap) -> wkp_status
            {
                return wkp_encode_f64_into(
                    values,
                    value_count,
                    dimensions,
                    precisions,
                    precision_count,
                    out,
                    err,
                    err_cap);
            });
    }

    wkp_status wkp_workspace_encode_point_f64(
        wkp_workspace *workspace,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        return workspace_encode_u8_retry(
            workspace,
            out_data,
            out_size,
            error_message,
            error_message_capacity,
            [&](wkp_u8_buffer *out, char *err, size_t err_cap) -> wkp_status
            {
                return wkp_encode_point_f64_into(
                    coords,
                    coord_value_count,
                    dimensions,
                    precision,
                    out,
                    err,
                    err_cap);
            });
    }

    wkp_status wkp_workspace_encode_linestring_f64(
        wkp_workspace *workspace,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        return workspace_encode_u8_retry(
            workspace,
            out_data,
            out_size,
            error_message,
            error_message_capacity,
            [&](wkp_u8_buffer *out, char *err, size_t err_cap) -> wkp_status
            {
                return wkp_encode_linestring_f64_into(
                    coords,
                    coord_value_count,
                    dimensions,
                    precision,
                    out,
                    err,
                    err_cap);
            });
    }

    wkp_status wkp_workspace_encode_polygon_f64(
        wkp_workspace *workspace,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *ring_point_counts,
        size_t ring_count,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        return workspace_encode_u8_retry(
            workspace,
            out_data,
            out_size,
            error_message,
            error_message_capacity,
            [&](wkp_u8_buffer *out, char *err, size_t err_cap) -> wkp_status
            {
                return wkp_encode_polygon_f64_into(
                    coords,
                    coord_value_count,
                    dimensions,
                    precision,
                    ring_point_counts,
                    ring_count,
                    out,
                    err,
                    err_cap);
            });
    }

    wkp_status wkp_workspace_encode_multipoint_f64(
        wkp_workspace *workspace,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        size_t point_count,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        return workspace_encode_u8_retry(
            workspace,
            out_data,
            out_size,
            error_message,
            error_message_capacity,
            [&](wkp_u8_buffer *out, char *err, size_t err_cap) -> wkp_status
            {
                return wkp_encode_multipoint_f64_into(
                    coords,
                    coord_value_count,
                    dimensions,
                    precision,
                    point_count,
                    out,
                    err,
                    err_cap);
            });
    }

    wkp_status wkp_workspace_encode_multilinestring_f64(
        wkp_workspace *workspace,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *linestring_point_counts,
        size_t linestring_count,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        return workspace_encode_u8_retry(
            workspace,
            out_data,
            out_size,
            error_message,
            error_message_capacity,
            [&](wkp_u8_buffer *out, char *err, size_t err_cap) -> wkp_status
            {
                return wkp_encode_multilinestring_f64_into(
                    coords,
                    coord_value_count,
                    dimensions,
                    precision,
                    linestring_point_counts,
                    linestring_count,
                    out,
                    err,
                    err_cap);
            });
    }

    wkp_status wkp_workspace_encode_multipolygon_f64(
        wkp_workspace *workspace,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *polygon_ring_counts,
        size_t polygon_count,
        const size_t *ring_point_counts,
        size_t ring_count,
        const uint8_t **out_data,
        size_t *out_size,
        char *error_message,
        size_t error_message_capacity)
    {
        return workspace_encode_u8_retry(
            workspace,
            out_data,
            out_size,
            error_message,
            error_message_capacity,
            [&](wkp_u8_buffer *out, char *err, size_t err_cap) -> wkp_status
            {
                return wkp_encode_multipolygon_f64_into(
                    coords,
                    coord_value_count,
                    dimensions,
                    precision,
                    polygon_ring_counts,
                    polygon_count,
                    ring_point_counts,
                    ring_count,
                    out,
                    err,
                    err_cap);
            });
    }

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
        size_t error_message_capacity)
    {
        if (workspace == nullptr || out_data == nullptr || out_size == nullptr)
        {
            set_error(error_message, error_message_capacity, "workspace, out_data, and out_size are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        if (workspace->f64_work.empty())
        {
            constexpr std::size_t kDefaultCapacity = 16;
            const wkp_status reserve_status = resize_workspace_f64(
                workspace,
                kDefaultCapacity,
                error_message,
                error_message_capacity);
            if (reserve_status != WKP_STATUS_OK)
            {
                return reserve_status;
            }
        }

        for (;;)
        {
            wkp_f64_buffer out{
                workspace->f64_work.data(),
                workspace->f64_work.size()};

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
                const wkp_status reserve_status = resize_workspace_f64(
                    workspace,
                    out.size,
                    error_message,
                    error_message_capacity);
                if (reserve_status != WKP_STATUS_OK)
                {
                    return reserve_status;
                }
                continue;
            }

            if (status != WKP_STATUS_OK)
            {
                return status;
            }

            *out_data = workspace->f64_work.data();
            *out_size = out.size;
            return WKP_STATUS_OK;
        }
    }

    wkp_status wkp_decode_geometry_header(
        const uint8_t *encoded,
        size_t encoded_size,
        int *out_version,
        int *out_precision,
        int *out_dimensions,
        int *out_geometry_type,
        char *error_message,
        size_t error_message_capacity)
    {
        if (encoded == nullptr)
        {
            set_error(error_message, error_message_capacity, "encoded is required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (out_version == nullptr || out_precision == nullptr || out_dimensions == nullptr || out_geometry_type == nullptr)
        {
            set_error(error_message, error_message_capacity, "all output pointers are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        try
        {
            const std::string_view encoded_view(
                reinterpret_cast<const char *>(encoded),
                encoded_size);
            const auto header = wkp::core::decode_geometry_header(encoded_view);

            *out_version = header.version;
            *out_precision = header.precision;
            *out_dimensions = header.dimensions;
            *out_geometry_type = header.geometry_type;
            return WKP_STATUS_OK;
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

    wkp_status wkp_encode_point_f64_into(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_encode_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if ((coord_value_count / dimensions) != 1)
        {
            set_error(error_message, error_message_capacity, "POINT requires exactly 1 coordinate");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        init_uniform_factors(dimensions, precision, factors);

        OutputWriter writer{out_encoded->data, out_encoded->size, 0, false};
        if (!write_geometry_header(writer, dimensions, precision, WKP_GEOMETRY_POINT, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        append_encoded_segment(writer, coords, coord_value_count, dimensions, factors);
        return finalize_encoded_result(writer, out_encoded, error_message, error_message_capacity);
    }

    wkp_status wkp_encode_linestring_f64_into(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_encode_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if ((coord_value_count / dimensions) < 2)
        {
            set_error(error_message, error_message_capacity, "LINESTRING requires at least 2 coordinates");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        init_uniform_factors(dimensions, precision, factors);

        OutputWriter writer{out_encoded->data, out_encoded->size, 0, false};
        if (!write_geometry_header(writer, dimensions, precision, WKP_GEOMETRY_LINESTRING, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        append_encoded_segment(writer, coords, coord_value_count, dimensions, factors);
        return finalize_encoded_result(writer, out_encoded, error_message, error_message_capacity);
    }

    wkp_status wkp_encode_polygon_f64_into(
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
        if (!validate_encode_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (ring_point_counts == nullptr)
        {
            set_error(error_message, error_message_capacity, "ring_point_counts is required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (ring_count == 0)
        {
            set_error(error_message, error_message_capacity, "POLYGON requires at least one ring");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t total_points = 0;
        for (std::size_t i = 0; i < ring_count; ++i)
        {
            total_points += ring_point_counts[i];
        }
        if (total_points * dimensions != coord_value_count)
        {
            set_error(error_message, error_message_capacity, "ring point counts do not match coord_value_count");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        init_uniform_factors(dimensions, precision, factors);

        OutputWriter writer{out_encoded->data, out_encoded->size, 0, false};
        if (!write_geometry_header(writer, dimensions, precision, WKP_GEOMETRY_POLYGON, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t offset = 0;
        for (std::size_t i = 0; i < ring_count; ++i)
        {
            if (i > 0)
            {
                writer.push(kSepRing);
            }
            const std::size_t point_count = ring_point_counts[i];
            append_encoded_segment(writer, coords + offset, point_count * dimensions, dimensions, factors);
            offset += point_count * dimensions;
        }

        return finalize_encoded_result(writer, out_encoded, error_message, error_message_capacity);
    }

    wkp_status wkp_encode_multipoint_f64_into(
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        size_t point_count,
        wkp_u8_buffer *out_encoded,
        char *error_message,
        size_t error_message_capacity)
    {
        if (!validate_encode_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (point_count * dimensions != coord_value_count)
        {
            set_error(error_message, error_message_capacity, "point_count does not match coord_value_count");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        init_uniform_factors(dimensions, precision, factors);

        OutputWriter writer{out_encoded->data, out_encoded->size, 0, false};
        if (!write_geometry_header(writer, dimensions, precision, WKP_GEOMETRY_MULTIPOINT, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t offset = 0;
        for (std::size_t i = 0; i < point_count; ++i)
        {
            if (i > 0)
            {
                writer.push(kSepMulti);
            }
            append_encoded_segment(writer, coords + offset, dimensions, dimensions, factors);
            offset += dimensions;
        }

        return finalize_encoded_result(writer, out_encoded, error_message, error_message_capacity);
    }

    wkp_status wkp_encode_multilinestring_f64_into(
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
        if (!validate_encode_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (linestring_point_counts == nullptr)
        {
            set_error(error_message, error_message_capacity, "linestring_point_counts is required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t total_points = 0;
        for (std::size_t i = 0; i < linestring_count; ++i)
        {
            if (linestring_point_counts[i] < 2)
            {
                set_error(error_message, error_message_capacity, "Each MULTILINESTRING part must contain at least two coordinates");
                return WKP_STATUS_INVALID_ARGUMENT;
            }
            total_points += linestring_point_counts[i];
        }
        if (total_points * dimensions != coord_value_count)
        {
            set_error(error_message, error_message_capacity, "linestring point counts do not match coord_value_count");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        init_uniform_factors(dimensions, precision, factors);

        OutputWriter writer{out_encoded->data, out_encoded->size, 0, false};
        if (!write_geometry_header(writer, dimensions, precision, WKP_GEOMETRY_MULTILINESTRING, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t offset = 0;
        for (std::size_t i = 0; i < linestring_count; ++i)
        {
            if (i > 0)
            {
                writer.push(kSepMulti);
            }
            const std::size_t point_count = linestring_point_counts[i];
            append_encoded_segment(writer, coords + offset, point_count * dimensions, dimensions, factors);
            offset += point_count * dimensions;
        }

        return finalize_encoded_result(writer, out_encoded, error_message, error_message_capacity);
    }

    wkp_status wkp_encode_multipolygon_f64_into(
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
        if (!validate_encode_inputs(coords, coord_value_count, dimensions, out_encoded, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (polygon_ring_counts == nullptr || ring_point_counts == nullptr)
        {
            set_error(error_message, error_message_capacity, "polygon_ring_counts and ring_point_counts are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t ring_total = 0;
        for (std::size_t i = 0; i < polygon_count; ++i)
        {
            ring_total += polygon_ring_counts[i];
        }
        if (ring_total != ring_count)
        {
            set_error(error_message, error_message_capacity, "polygon_ring_counts must sum to ring_count");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t total_points = 0;
        for (std::size_t i = 0; i < ring_count; ++i)
        {
            total_points += ring_point_counts[i];
        }
        if (total_points * dimensions != coord_value_count)
        {
            set_error(error_message, error_message_capacity, "ring_point_counts do not match coord_value_count");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        double factors[kMaxDimensions] = {0.0};
        init_uniform_factors(dimensions, precision, factors);

        OutputWriter writer{out_encoded->data, out_encoded->size, 0, false};
        if (!write_geometry_header(writer, dimensions, precision, WKP_GEOMETRY_MULTIPOLYGON, error_message, error_message_capacity))
        {
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::size_t coord_offset = 0;
        std::size_t ring_offset = 0;
        for (std::size_t poly_idx = 0; poly_idx < polygon_count; ++poly_idx)
        {
            if (poly_idx > 0)
            {
                writer.push(kSepMulti);
            }

            const std::size_t poly_ring_count = polygon_ring_counts[poly_idx];
            if (poly_ring_count == 0)
            {
                set_error(error_message, error_message_capacity, "Each MULTIPOLYGON part requires at least one ring");
                return WKP_STATUS_INVALID_ARGUMENT;
            }

            for (std::size_t ring_idx = 0; ring_idx < poly_ring_count; ++ring_idx)
            {
                if (ring_idx > 0)
                {
                    writer.push(kSepRing);
                }

                const std::size_t point_count = ring_point_counts[ring_offset++];
                append_encoded_segment(writer, coords + coord_offset, point_count * dimensions, dimensions, factors);
                coord_offset += point_count * dimensions;
            }
        }

        return finalize_encoded_result(writer, out_encoded, error_message, error_message_capacity);
    }

    void wkp_free_u8_buffer(wkp_u8_buffer *buffer)
    {
        if (buffer == nullptr)
        {
            return;
        }

        buffer->data = nullptr;
        buffer->size = 0;
    }

    void wkp_free_f64_buffer(wkp_f64_buffer *buffer)
    {
        if (buffer == nullptr)
        {
            return;
        }

        buffer->data = nullptr;
        buffer->size = 0;
    }

} // extern "C"
