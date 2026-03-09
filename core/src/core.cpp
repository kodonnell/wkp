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
    std::vector<size_t> size_work_a;
    std::vector<size_t> size_work_b;
    wkp_geometry_frame_f64 geometry_frame_work{};
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
    constexpr int kHeaderFieldOffset = 63;
    constexpr int kHeaderFieldMax = 63;

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
        const std::vector<double> &factors,
        std::vector<long long> *previous_state = nullptr)
    {
        std::vector<long long> previous_local;
        if (previous_state == nullptr)
        {
            previous_local.assign(dimensions, 0LL);
            previous_state = &previous_local;
        }
        else if (previous_state->size() != dimensions)
        {
            previous_state->assign(dimensions, 0LL);
        }

        for (std::size_t i = 0; i < value_count; ++i)
        {
            const std::size_t dim = i % dimensions;
            const long long scaled = static_cast<long long>(std::llround(values[i] * factors[dim]));
            const long long delta = scaled - (*previous_state)[dim];
            (*previous_state)[dim] = scaled;
            append_encoded_signed(out, delta);
        }
    }

    int decode_header_field(char c)
    {
        const int decoded = static_cast<int>(static_cast<unsigned char>(c)) - kHeaderFieldOffset;
        if (decoded < 0 || decoded > kHeaderFieldMax)
        {
            throw std::invalid_argument("Invalid geometry header field");
        }
        return decoded;
    }

    char encode_header_field(int value)
    {
        if (value < 0 || value > kHeaderFieldMax)
        {
            throw std::invalid_argument("geometry header field out of range");
        }
        return static_cast<char>(value + kHeaderFieldOffset);
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
        if (precision < 0 || precision > kHeaderFieldMax)
        {
            throw std::invalid_argument("precision must be between 0 and 63");
        }
        const int geometry_code = static_cast<int>(geometry_type);
        if (geometry_code < 0 || geometry_code > kHeaderFieldMax)
        {
            throw std::invalid_argument("geometry type must be between 0 and 63");
        }

        std::string header;
        header.reserve(4);
        header.push_back(encode_header_field(kGeometryVersion));
        header.push_back(encode_header_field(precision));
        header.push_back(encode_header_field(static_cast<int>(dimensions)));
        header.push_back(encode_header_field(geometry_code));
        return header;
    }

    void append_coords_segment(
        std::string &out,
        const double *coords,
        std::size_t point_count,
        std::size_t dimensions,
        const std::vector<double> &factors,
        std::vector<long long> *previous_state = nullptr)
    {
        if (coords == nullptr)
        {
            throw std::invalid_argument("coords pointer cannot be null");
        }
        append_encoded_segment(out, coords, point_count * dimensions, dimensions, factors, previous_state);
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
        out.reserve(4 + coord_value_count * 4 + 8);
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
        out.reserve(4 + coord_value_count * 4 + 8);
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
        out.reserve(4 + coord_value_count * 4 + ring_count + 8);

        std::vector<long long> previous(dimensions, 0LL);
        std::size_t offset = 0;
        for (std::size_t i = 0; i < ring_count; ++i)
        {
            if (i > 0)
            {
                out.push_back(kSepRing);
            }
            const std::size_t point_count = ring_point_counts[i];
            append_coords_segment(out, coords + offset, point_count, dimensions, factors, &previous);
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
        out.reserve(4 + coord_value_count * 4 + point_count + 8);

        std::vector<long long> previous(dimensions, 0LL);
        std::size_t offset = 0;
        for (std::size_t i = 0; i < point_count; ++i)
        {
            if (i > 0)
            {
                out.push_back(kSepMulti);
            }
            append_coords_segment(out, coords + offset, 1, dimensions, factors, &previous);
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
        out.reserve(4 + coord_value_count * 4 + linestring_count + 8);

        std::vector<long long> previous(dimensions, 0LL);
        std::size_t offset = 0;
        for (std::size_t i = 0; i < linestring_count; ++i)
        {
            if (i > 0)
            {
                out.push_back(kSepMulti);
            }
            const std::size_t point_count = linestring_point_counts[i];
            append_coords_segment(out, coords + offset, point_count, dimensions, factors, &previous);
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
        out.reserve(4 + coord_value_count * 4 + ring_count + polygon_count + 8);

        std::vector<long long> previous(dimensions, 0LL);
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
                append_coords_segment(out, coords + coord_offset, point_count, dimensions, factors, &previous);
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
        if (encoded.size() < 4)
        {
            throw std::invalid_argument("Encoded geometry header too short");
        }

        const int version = decode_header_field(encoded[0]);
        if (version != kGeometryVersion)
        {
            throw std::invalid_argument("Unsupported geometry encoding version");
        }

        return GeometryHeader{
            version,
            decode_header_field(encoded[1]),
            decode_header_field(encoded[2]),
            decode_header_field(encoded[3]),
        };
    }

    GeometryFrame decode_geometry_frame(std::string_view encoded)
    {
        const GeometryHeader header = decode_geometry_header(encoded);
        const std::size_t body_start = 4;
        const std::size_t body_end = encoded.size();
        const std::size_t dims = static_cast<std::size_t>(header.dimensions);
        const auto factors = build_uniform_factors(dims, header.precision);
        std::vector<long long> previous(dims, 0LL);

        auto decode_segment = [&](std::size_t start, std::size_t end) -> std::vector<double>
        {
            if (end < start || end == start)
            {
                throw std::invalid_argument("Malformed encoded geometry segment");
            }
            std::vector<double> decoded;
            std::size_t index = start;
            std::size_t value_count = 0;
            while (index < end)
            {
                const std::size_t dim = value_count % dims;
                previous[dim] += decode_once(encoded, index);
                if (index > end)
                {
                    throw std::invalid_argument("Malformed encoded geometry segment");
                }
                decoded.push_back(static_cast<double>(previous[dim]) / factors[dim]);
                ++value_count;
            }
            if ((value_count % dims) != 0)
            {
                throw std::invalid_argument("Malformed encoded coordinate stream");
            }
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

        std::vector<double> factors(dimensions, 1.0);
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = std::pow(10.0, static_cast<double>(p[i]));
        }

        out_encoded.clear();
        out_encoded.reserve((value_count * 4) + 8);
        append_encoded_segment(out_encoded, values, value_count, dimensions, factors);
    }

    void decode_f64_into(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions,
        std::vector<double> &out_values)
    {
        const std::vector<int> p = normalize_precisions(dimensions, precisions);

        std::vector<double> factors(dimensions, 1.0);
        for (std::size_t i = 0; i < dimensions; ++i)
        {
            factors[i] = std::pow(10.0, static_cast<double>(p[i]));
        }

        std::vector<long long> previous(dimensions, 0LL);
        out_values.clear();
        out_values.reserve(std::max<std::size_t>(16, encoded.size()));

        std::size_t index = 0;
        std::size_t value_count = 0;
        while (index < encoded.size())
        {
            const std::size_t dim = value_count % dimensions;
            previous[dim] += decode_once(encoded, index);
            out_values.push_back(static_cast<double>(previous[dim]) / factors[dim]);
            ++value_count;
        }

        if ((value_count % dimensions) != 0)
        {
            throw std::invalid_argument("Malformed encoded coordinate stream");
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
        const double *factors,
        long long *previous_state = nullptr)
    {
        long long previous_local[kMaxDimensions] = {0};
        long long *previous = (previous_state != nullptr) ? previous_state : previous_local;
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
        if (precision < 0 || precision > kHeaderFieldMax)
        {
            set_error(error_message, error_message_capacity, "precision must be between 0 and 63");
            return false;
        }

        const int geometry_code = static_cast<int>(geometry_type);
        if (precision > kHeaderFieldMax || static_cast<int>(dimensions) > kHeaderFieldMax || geometry_code < 0 || geometry_code > kHeaderFieldMax)
        {
            set_error(error_message, error_message_capacity, "geometry header field out of range");
            return false;
        }

        out.push(static_cast<char>(kGeometryVersion + kHeaderFieldOffset));
        out.push(static_cast<char>(precision + kHeaderFieldOffset));
        out.push(static_cast<char>(static_cast<int>(dimensions) + kHeaderFieldOffset));
        out.push(static_cast<char>(geometry_code + kHeaderFieldOffset));
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

} // namespace

extern "C"
{

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
        if (workspace == nullptr || out_data == nullptr || out_size == nullptr)
        {
            set_error(error_message, error_message_capacity, "workspace, out_data, and out_size are required");
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

        if (workspace->u8_work.capacity() == 0)
        {
            const wkp_status resize_status = resize_workspace_u8(
                workspace,
                64,
                error_message,
                error_message_capacity);
            if (resize_status != WKP_STATUS_OK)
            {
                return resize_status;
            }
        }

        for (;;)
        {
            OutputWriter writer{
                workspace->u8_work.data(),
                workspace->u8_work.size(),
                0,
                false};
            append_encoded_segment(writer, values, value_count, dimensions, factors);

            if (writer.overflow)
            {
                const wkp_status resize_status = resize_workspace_u8(
                    workspace,
                    writer.written,
                    error_message,
                    error_message_capacity);
                if (resize_status != WKP_STATUS_OK)
                {
                    return resize_status;
                }
                continue;
            }

            *out_data = workspace->u8_work.data();
            *out_size = writer.written;
            return WKP_STATUS_OK;
        }
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

        std::size_t current_capacity = workspace->f64_work.size();
        if (current_capacity == 0)
        {
            current_capacity = workspace->has_f64_limit ? std::min<std::size_t>(16, workspace->max_f64_capacity) : 16;
        }
        if (current_capacity > 0)
        {
            const wkp_status resize_status = resize_workspace_f64(
                workspace,
                current_capacity,
                error_message,
                error_message_capacity);
            if (resize_status != WKP_STATUS_OK)
            {
                return resize_status;
            }
        }

        try
        {
            const std::string_view encoded_view(reinterpret_cast<const char *>(encoded), encoded_size);
            long long previous[kMaxDimensions] = {0};
            std::size_t value_count = 0;
            std::size_t index = 0;

            while (index < encoded_view.size())
            {
                if (workspace->has_f64_limit && value_count >= workspace->max_f64_capacity)
                {
                    set_error(error_message, error_message_capacity, "workspace f64 buffer exceeded max_size");
                    return WKP_STATUS_LIMIT_EXCEEDED;
                }

                if (value_count >= workspace->f64_work.size())
                {
                    std::size_t next_capacity = std::max<std::size_t>(16, workspace->f64_work.size() == 0 ? 16 : workspace->f64_work.size() * 2);
                    if (workspace->has_f64_limit)
                    {
                        next_capacity = std::min(next_capacity, workspace->max_f64_capacity);
                    }
                    if (next_capacity <= workspace->f64_work.size())
                    {
                        set_error(error_message, error_message_capacity, "workspace f64 buffer exceeded max_size");
                        return WKP_STATUS_LIMIT_EXCEEDED;
                    }
                    const wkp_status resize_status = resize_workspace_f64(
                        workspace,
                        next_capacity,
                        error_message,
                        error_message_capacity);
                    if (resize_status != WKP_STATUS_OK)
                    {
                        return resize_status;
                    }
                }

                const std::size_t dim = value_count % dimensions;
                previous[dim] += decode_once(encoded_view, index);
                workspace->f64_work[value_count] = static_cast<double>(previous[dim]) / factors[dim];
                ++value_count;
            }

            if ((value_count % dimensions) != 0U)
            {
                set_error(error_message, error_message_capacity, "Malformed encoded coordinate stream");
                return WKP_STATUS_MALFORMED_INPUT;
            }

            workspace->f64_work.resize(value_count);
            *out_data = workspace->f64_work.data();
            *out_size = value_count;
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
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "failed to allocate workspace decode buffer");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }
    }

    wkp_status wkp_workspace_encode_geometry_frame_f64(
        wkp_workspace *workspace,
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
        if (workspace == nullptr || out_data == nullptr || out_size == nullptr)
        {
            set_error(error_message, error_message_capacity, "workspace, out_data, and out_size are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (coords == nullptr)
        {
            set_error(error_message, error_message_capacity, "coords is required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (dimensions == 0 || dimensions > kMaxDimensions)
        {
            set_error(error_message, error_message_capacity, "dimensions must be between 1 and 16");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if ((coord_value_count % dimensions) != 0)
        {
            set_error(error_message, error_message_capacity, "coord_value_count must be divisible by dimensions");
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        if (group_segment_counts == nullptr || segment_point_counts == nullptr)
        {
            set_error(error_message, error_message_capacity, "group_segment_counts and segment_point_counts are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        std::string encoded;

        try
        {
            switch (geometry_type)
            {
            case WKP_GEOMETRY_POINT:
                if (group_count != 1 || segment_count != 1 || group_segment_counts[0] != 1 || segment_point_counts[0] != 1)
                {
                    set_error(error_message, error_message_capacity, "POINT frame requires one group with one single-point segment");
                    return WKP_STATUS_INVALID_ARGUMENT;
                }
                encoded = encode_geometry_point(coords, coord_value_count, dimensions, precision);
                break;

            case WKP_GEOMETRY_LINESTRING:
                if (group_count != 1 || segment_count != 1 || group_segment_counts[0] != 1)
                {
                    set_error(error_message, error_message_capacity, "LINESTRING frame requires one group with one segment");
                    return WKP_STATUS_INVALID_ARGUMENT;
                }
                encoded = encode_geometry_linestring(coords, coord_value_count, dimensions, precision);
                break;

            case WKP_GEOMETRY_POLYGON:
                if (group_count != 1 || group_segment_counts[0] != segment_count)
                {
                    set_error(error_message, error_message_capacity, "POLYGON frame requires one group with segment_count rings");
                    return WKP_STATUS_INVALID_ARGUMENT;
                }
                encoded = encode_geometry_polygon(coords, coord_value_count, dimensions, precision, segment_point_counts, segment_count);
                break;

            case WKP_GEOMETRY_MULTIPOINT:
                if (group_count != segment_count)
                {
                    set_error(error_message, error_message_capacity, "MULTIPOINT requires one segment per group");
                    return WKP_STATUS_INVALID_ARGUMENT;
                }
                for (size_t i = 0; i < group_count; ++i)
                {
                    if (group_segment_counts[i] != 1)
                    {
                        set_error(error_message, error_message_capacity, "MULTIPOINT groups must each contain one segment");
                        return WKP_STATUS_INVALID_ARGUMENT;
                    }
                }
                for (size_t i = 0; i < segment_count; ++i)
                {
                    if (segment_point_counts[i] != 1)
                    {
                        set_error(error_message, error_message_capacity, "MULTIPOINT segments must each contain one point");
                        return WKP_STATUS_INVALID_ARGUMENT;
                    }
                }
                encoded = encode_geometry_multipoint(coords, coord_value_count, dimensions, precision, segment_count);
                break;

            case WKP_GEOMETRY_MULTILINESTRING:
                if (group_count != segment_count)
                {
                    set_error(error_message, error_message_capacity, "MULTILINESTRING requires one segment per group");
                    return WKP_STATUS_INVALID_ARGUMENT;
                }
                for (size_t i = 0; i < group_count; ++i)
                {
                    if (group_segment_counts[i] != 1)
                    {
                        set_error(error_message, error_message_capacity, "MULTILINESTRING groups must each contain one segment");
                        return WKP_STATUS_INVALID_ARGUMENT;
                    }
                }
                encoded = encode_geometry_multilinestring(coords, coord_value_count, dimensions, precision, segment_point_counts, segment_count);
                break;

            case WKP_GEOMETRY_MULTIPOLYGON:
            {
                size_t summed_segments = 0;
                for (size_t i = 0; i < group_count; ++i)
                {
                    summed_segments += group_segment_counts[i];
                }
                if (summed_segments != segment_count)
                {
                    set_error(error_message, error_message_capacity, "MULTIPOLYGON group_segment_counts must sum to segment_count");
                    return WKP_STATUS_INVALID_ARGUMENT;
                }
                encoded = encode_geometry_multipolygon(
                    coords,
                    coord_value_count,
                    dimensions,
                    precision,
                    group_segment_counts,
                    group_count,
                    segment_point_counts,
                    segment_count);
                break;
            }

            default:
                set_error(error_message, error_message_capacity, "Unsupported geometry_type for geometry frame encoding");
                return WKP_STATUS_INVALID_ARGUMENT;
            }
        }
        catch (const std::invalid_argument &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "failed to allocate geometry encoding output");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
        }

        const wkp_status resize_status = resize_workspace_u8(
            workspace,
            encoded.size(),
            error_message,
            error_message_capacity);
        if (resize_status != WKP_STATUS_OK)
        {
            return resize_status;
        }

        if (!encoded.empty())
        {
            std::memcpy(workspace->u8_work.data(), encoded.data(), encoded.size());
        }

        *out_data = workspace->u8_work.data();
        *out_size = encoded.size();
        return WKP_STATUS_OK;
    }

    wkp_status wkp_workspace_decode_geometry_frame_f64(
        wkp_workspace *workspace,
        const uint8_t *encoded,
        size_t encoded_size,
        const wkp_geometry_frame_f64 **out_frame,
        char *error_message,
        size_t error_message_capacity)
    {
        if (workspace == nullptr || encoded == nullptr || out_frame == nullptr)
        {
            set_error(error_message, error_message_capacity, "workspace, encoded, and out_frame are required");
            return WKP_STATUS_INVALID_ARGUMENT;
        }

        try
        {
            const auto frame = wkp::core::decode_geometry_frame(
                std::string_view(reinterpret_cast<const char *>(encoded), encoded_size));

            std::size_t total_values = 0;
            std::size_t total_segments = 0;
            for (const auto &group : frame.groups)
            {
                total_segments += group.size();
                for (const auto &segment : group)
                {
                    total_values += segment.size();
                }
            }

            if (workspace->has_f64_limit && total_values > workspace->max_f64_capacity)
            {
                set_error(error_message, error_message_capacity, "workspace f64 buffer exceeded max_size");
                return WKP_STATUS_LIMIT_EXCEEDED;
            }

            workspace->f64_work.clear();
            workspace->f64_work.reserve(total_values);
            workspace->size_work_a.clear();
            workspace->size_work_a.reserve(total_segments);
            workspace->size_work_b.clear();
            workspace->size_work_b.reserve(frame.groups.size());

            const std::size_t dims = static_cast<std::size_t>(frame.header.dimensions);
            for (const auto &group : frame.groups)
            {
                workspace->size_work_b.push_back(group.size());
                for (const auto &segment : group)
                {
                    if ((segment.size() % dims) != 0)
                    {
                        set_error(error_message, error_message_capacity, "Decoded geometry segment has invalid coordinate length");
                        return WKP_STATUS_INTERNAL_ERROR;
                    }
                    workspace->size_work_a.push_back(segment.size() / dims);
                    workspace->f64_work.insert(workspace->f64_work.end(), segment.begin(), segment.end());
                }
            }

            workspace->geometry_frame_work.version = frame.header.version;
            workspace->geometry_frame_work.precision = frame.header.precision;
            workspace->geometry_frame_work.dimensions = frame.header.dimensions;
            workspace->geometry_frame_work.geometry_type = frame.header.geometry_type;
            workspace->geometry_frame_work.coords = workspace->f64_work.empty() ? nullptr : workspace->f64_work.data();
            workspace->geometry_frame_work.coord_value_count = workspace->f64_work.size();
            workspace->geometry_frame_work.segment_point_counts = workspace->size_work_a.empty() ? nullptr : workspace->size_work_a.data();
            workspace->geometry_frame_work.segment_count = workspace->size_work_a.size();
            workspace->geometry_frame_work.group_segment_counts = workspace->size_work_b.empty() ? nullptr : workspace->size_work_b.data();
            workspace->geometry_frame_work.group_count = workspace->size_work_b.size();

            *out_frame = &workspace->geometry_frame_work;
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
        catch (const std::bad_alloc &)
        {
            set_error(error_message, error_message_capacity, "failed to allocate geometry frame workspace buffers");
            return WKP_STATUS_ALLOCATION_FAILED;
        }
        catch (const std::exception &ex)
        {
            set_error(error_message, error_message_capacity, ex.what());
            return WKP_STATUS_INTERNAL_ERROR;
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

} // extern "C"
