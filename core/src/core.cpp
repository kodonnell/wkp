#include "wkp/core.h"
#include "wkp/core.hpp"
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

    GeometryEncoder::GeometryEncoder(int precision, int dimensions, std::size_t initial_capacity)
        : precision_(precision), dimensions_(dimensions), factors_(std::size_t(dimensions), 1.0), header_prefix_(), work_()
    {
        if (dimensions <= 0 || dimensions > static_cast<int>(kMaxDimensions))
        {
            throw std::invalid_argument("dimensions must be between 1 and 16");
        }

        for (int i = 0; i < dimensions_; ++i)
        {
            factors_[std::size_t(i)] = std::pow(10.0, static_cast<double>(precision_));
        }

        header_prefix_.reserve(6);
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d%02d%02d", kGeometryVersion, precision_, dimensions_);
            header_prefix_ = buf;
        }

        if (initial_capacity < 64)
        {
            initial_capacity = 64;
        }
        work_.reserve(initial_capacity);
    }

    int GeometryEncoder::precision() const noexcept { return precision_; }
    int GeometryEncoder::dimensions() const noexcept { return dimensions_; }

    void GeometryEncoder::reset_with_header(EncodedGeometryType geometry_type)
    {
        work_.clear();
        work_.append(header_prefix_);
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02d", static_cast<int>(geometry_type));
        work_.append(buf);
    }

    void GeometryEncoder::append_separator(char sep) { work_.push_back(sep); }

    void GeometryEncoder::append_coords_segment(const double *coords, std::size_t point_count)
    {
        if (coords == nullptr)
        {
            throw std::invalid_argument("coords pointer cannot be null");
        }
        const std::size_t value_count = point_count * static_cast<std::size_t>(dimensions_);
        append_encoded_segment(work_, coords, value_count, static_cast<std::size_t>(dimensions_), factors_);
    }

    std::string GeometryEncoder::encode_point(const double *coords, std::size_t point_count)
    {
        if (point_count != 1)
        {
            throw std::invalid_argument("POINT requires exactly 1 coordinate");
        }
        reset_with_header(EncodedGeometryType::POINT);
        append_coords_segment(coords, point_count);
        return work_;
    }

    std::string GeometryEncoder::encode_linestring(const double *coords, std::size_t point_count)
    {
        if (point_count < 2)
        {
            throw std::invalid_argument("LINESTRING requires at least 2 coordinates");
        }
        reset_with_header(EncodedGeometryType::LINESTRING);
        append_coords_segment(coords, point_count);
        return work_;
    }

    std::string GeometryEncoder::encode_polygon(
        const std::vector<const double *> &ring_coords,
        const std::vector<std::size_t> &ring_point_counts)
    {
        if (ring_coords.size() != ring_point_counts.size())
        {
            throw std::invalid_argument("ring_coords and ring_point_counts size mismatch");
        }
        if (ring_coords.empty())
        {
            throw std::invalid_argument("POLYGON requires at least one ring");
        }

        reset_with_header(EncodedGeometryType::POLYGON);
        for (std::size_t i = 0; i < ring_coords.size(); ++i)
        {
            if (i > 0)
            {
                append_separator(kSepRing);
            }
            append_coords_segment(ring_coords[i], ring_point_counts[i]);
        }
        return work_;
    }

    std::string GeometryEncoder::encode_multipoint(
        const std::vector<const double *> &point_coords,
        const std::vector<std::size_t> &point_counts)
    {
        if (point_coords.size() != point_counts.size())
        {
            throw std::invalid_argument("point_coords and point_counts size mismatch");
        }
        reset_with_header(EncodedGeometryType::MULTIPOINT);
        for (std::size_t i = 0; i < point_coords.size(); ++i)
        {
            if (point_counts[i] != 1)
            {
                throw std::invalid_argument("Each MULTIPOINT part must contain exactly one coordinate");
            }
            if (i > 0)
            {
                append_separator(kSepMulti);
            }
            append_coords_segment(point_coords[i], point_counts[i]);
        }
        return work_;
    }

    std::string GeometryEncoder::encode_multilinestring(
        const std::vector<const double *> &linestring_coords,
        const std::vector<std::size_t> &linestring_point_counts)
    {
        if (linestring_coords.size() != linestring_point_counts.size())
        {
            throw std::invalid_argument("linestring_coords and linestring_point_counts size mismatch");
        }
        reset_with_header(EncodedGeometryType::MULTILINESTRING);
        for (std::size_t i = 0; i < linestring_coords.size(); ++i)
        {
            if (linestring_point_counts[i] < 2)
            {
                throw std::invalid_argument("Each MULTILINESTRING part must contain at least two coordinates");
            }
            if (i > 0)
            {
                append_separator(kSepMulti);
            }
            append_coords_segment(linestring_coords[i], linestring_point_counts[i]);
        }
        return work_;
    }

    std::string GeometryEncoder::encode_multipolygon(
        const std::vector<std::vector<const double *>> &polygon_ring_coords,
        const std::vector<std::vector<std::size_t>> &polygon_ring_point_counts)
    {
        if (polygon_ring_coords.size() != polygon_ring_point_counts.size())
        {
            throw std::invalid_argument("polygon ring coordinate/count size mismatch");
        }

        reset_with_header(EncodedGeometryType::MULTIPOLYGON);
        for (std::size_t poly_idx = 0; poly_idx < polygon_ring_coords.size(); ++poly_idx)
        {
            const auto &rings = polygon_ring_coords[poly_idx];
            const auto &counts = polygon_ring_point_counts[poly_idx];
            if (rings.size() != counts.size())
            {
                throw std::invalid_argument("polygon ring coordinate/count size mismatch");
            }
            if (rings.empty())
            {
                throw std::invalid_argument("Each MULTIPOLYGON part requires at least one ring");
            }

            if (poly_idx > 0)
            {
                append_separator(kSepMulti);
            }

            for (std::size_t ring_idx = 0; ring_idx < rings.size(); ++ring_idx)
            {
                if (ring_idx > 0)
                {
                    append_separator(kSepRing);
                }
                append_coords_segment(rings[ring_idx], counts[ring_idx]);
            }
        }

        return work_;
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
