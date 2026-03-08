#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

    inline void throw_for_status(wkp_status status, const char *error)
    {
        if (status == WKP_STATUS_OK)
        {
            return;
        }

        const std::string msg = (error != nullptr && error[0] != '\0') ? std::string(error) : std::string("WKP error");
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

    class Workspace
    {
    public:
        Workspace(
            std::size_t initial_u8_capacity = 4096,
            std::size_t initial_f64_capacity = 256,
            int64_t max_u8_size = -1,
            int64_t max_f64_size = -1)
        {
            char error[512] = {0};
            const auto status = wkp_workspace_create(
                initial_u8_capacity,
                initial_f64_capacity,
                max_u8_size,
                max_f64_size,
                &workspace_,
                error,
                sizeof(error));
            throw_for_status(status, error);
            if (workspace_ == nullptr)
            {
                throw std::runtime_error("failed to create workspace");
            }
        }

        ~Workspace()
        {
            if (workspace_ != nullptr)
            {
                wkp_workspace_destroy(workspace_);
                workspace_ = nullptr;
            }
        }

        Workspace(const Workspace &) = delete;
        Workspace &operator=(const Workspace &) = delete;

        Workspace(Workspace &&other) noexcept : workspace_(other.workspace_)
        {
            other.workspace_ = nullptr;
        }

        Workspace &operator=(Workspace &&other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }
            if (workspace_ != nullptr)
            {
                wkp_workspace_destroy(workspace_);
            }
            workspace_ = other.workspace_;
            other.workspace_ = nullptr;
            return *this;
        }

        std::string encode_f64(const double *values, std::size_t value_count, std::size_t dimensions, const std::vector<int> &precisions)
        {
            const auto p = normalize_precisions(dimensions, precisions);
            const uint8_t *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_encode_f64(
                workspace_,
                values,
                value_count,
                dimensions,
                p.data(),
                p.size(),
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::string(reinterpret_cast<const char *>(data), size);
        }

        std::vector<double> decode_f64(std::string_view encoded, std::size_t dimensions, const std::vector<int> &precisions)
        {
            const auto p = normalize_precisions(dimensions, precisions);
            const double *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_decode_f64(
                workspace_,
                reinterpret_cast<const uint8_t *>(encoded.data()),
                encoded.size(),
                dimensions,
                p.data(),
                p.size(),
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::vector<double>(data, data + size);
        }

        std::string encode_point(const double *coords, std::size_t point_count, std::size_t dimensions, int precision)
        {
            const uint8_t *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_encode_point_f64(
                workspace_,
                coords,
                point_count * dimensions,
                dimensions,
                precision,
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::string(reinterpret_cast<const char *>(data), size);
        }

        std::string encode_linestring(const double *coords, std::size_t point_count, std::size_t dimensions, int precision)
        {
            const uint8_t *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_encode_linestring_f64(
                workspace_,
                coords,
                point_count * dimensions,
                dimensions,
                precision,
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::string(reinterpret_cast<const char *>(data), size);
        }

        std::string encode_polygon(
            const std::vector<const double *> &ring_coords,
            const std::vector<std::size_t> &ring_point_counts,
            std::size_t dimensions,
            int precision)
        {
            if (ring_coords.size() != ring_point_counts.size())
            {
                throw std::invalid_argument("ring coordinate/count size mismatch");
            }
            std::vector<double> flat;
            flat.reserve(1024);
            for (std::size_t i = 0; i < ring_coords.size(); ++i)
            {
                const std::size_t n = ring_point_counts[i] * dimensions;
                const double *src = ring_coords[i];
                if (src == nullptr)
                {
                    throw std::invalid_argument("ring coords pointer cannot be null");
                }
                const std::size_t old_size = flat.size();
                flat.resize(old_size + n);
                std::memcpy(flat.data() + old_size, src, n * sizeof(double));
            }

            const uint8_t *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_encode_polygon_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                ring_point_counts.data(),
                ring_point_counts.size(),
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::string(reinterpret_cast<const char *>(data), size);
        }

        std::string encode_multipoint(
            const std::vector<const double *> &point_coords,
            const std::vector<std::size_t> &point_counts,
            std::size_t dimensions,
            int precision)
        {
            if (point_coords.size() != point_counts.size())
            {
                throw std::invalid_argument("point coordinate/count size mismatch");
            }

            std::vector<double> flat;
            flat.reserve(point_coords.size() * dimensions);
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
                const std::size_t old_size = flat.size();
                flat.resize(old_size + dimensions);
                std::memcpy(flat.data() + old_size, src, dimensions * sizeof(double));
            }

            const uint8_t *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_encode_multipoint_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                point_coords.size(),
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::string(reinterpret_cast<const char *>(data), size);
        }

        std::string encode_multilinestring(
            const std::vector<const double *> &linestring_coords,
            const std::vector<std::size_t> &linestring_point_counts,
            std::size_t dimensions,
            int precision)
        {
            if (linestring_coords.size() != linestring_point_counts.size())
            {
                throw std::invalid_argument("linestring coordinate/count size mismatch");
            }

            std::vector<double> flat;
            std::vector<std::size_t> counts;
            counts.reserve(linestring_point_counts.size());
            flat.reserve(1024);
            for (std::size_t i = 0; i < linestring_coords.size(); ++i)
            {
                const double *src = linestring_coords[i];
                if (src == nullptr)
                {
                    throw std::invalid_argument("linestring coords pointer cannot be null");
                }
                const std::size_t n = linestring_point_counts[i] * dimensions;
                const std::size_t old_size = flat.size();
                flat.resize(old_size + n);
                std::memcpy(flat.data() + old_size, src, n * sizeof(double));
                counts.push_back(linestring_point_counts[i]);
            }

            const uint8_t *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_encode_multilinestring_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                counts.data(),
                counts.size(),
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::string(reinterpret_cast<const char *>(data), size);
        }

        std::string encode_multipolygon(
            const std::vector<std::vector<const double *>> &polygon_ring_coords,
            const std::vector<std::vector<std::size_t>> &polygon_ring_point_counts,
            std::size_t dimensions,
            int precision)
        {
            if (polygon_ring_coords.size() != polygon_ring_point_counts.size())
            {
                throw std::invalid_argument("polygon coordinate/count size mismatch");
            }

            std::vector<double> flat;
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
                    const std::size_t n = counts[i] * dimensions;
                    const double *src = rings[i];
                    if (src == nullptr)
                    {
                        throw std::invalid_argument("polygon ring coords pointer cannot be null");
                    }
                    const std::size_t old_size = flat.size();
                    flat.resize(old_size + n);
                    std::memcpy(flat.data() + old_size, src, n * sizeof(double));
                    ring_point_counts.push_back(counts[i]);
                }
            }

            const uint8_t *data = nullptr;
            size_t size = 0;
            char error[512] = {0};
            const auto status = wkp_workspace_encode_multipolygon_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                polygon_ring_counts.data(),
                polygon_ring_counts.size(),
                ring_point_counts.data(),
                ring_point_counts.size(),
                &data,
                &size,
                error,
                sizeof(error));
            throw_for_status(status, error);
            return std::string(reinterpret_cast<const char *>(data), size);
        }

        wkp_workspace *raw() const noexcept { return workspace_; }

    private:
        wkp_workspace *workspace_ = nullptr;
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

    inline std::string encode_f64(
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<int> &precisions,
        Workspace *workspace = nullptr)
    {
        if (workspace != nullptr)
        {
            return workspace->encode_f64(values, value_count, dimensions, precisions);
        }
        std::string out;
        encode_f64_into(values, value_count, dimensions, precisions, out);
        return out;
    }

    inline std::vector<double> decode_f64(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions,
        Workspace *workspace = nullptr)
    {
        if (workspace != nullptr)
        {
            return workspace->decode_f64(encoded, dimensions, precisions);
        }
        std::vector<double> out;
        decode_f64_into(encoded, dimensions, precisions, out);
        return out;
    }

    inline std::string encode_point(const double *coords, std::size_t point_count, std::size_t dimensions, int precision, Workspace *workspace = nullptr)
    {
        Workspace local;
        Workspace &ws = (workspace != nullptr) ? *workspace : local;
        return ws.encode_point(coords, point_count, dimensions, precision);
    }

    inline std::string encode_linestring(const double *coords, std::size_t point_count, std::size_t dimensions, int precision, Workspace *workspace = nullptr)
    {
        Workspace local;
        Workspace &ws = (workspace != nullptr) ? *workspace : local;
        return ws.encode_linestring(coords, point_count, dimensions, precision);
    }

    inline std::string encode_polygon(
        const std::vector<const double *> &ring_coords,
        const std::vector<std::size_t> &ring_point_counts,
        std::size_t dimensions,
        int precision,
        Workspace *workspace = nullptr)
    {
        Workspace local;
        Workspace &ws = (workspace != nullptr) ? *workspace : local;
        return ws.encode_polygon(ring_coords, ring_point_counts, dimensions, precision);
    }

    inline std::string encode_multipoint(
        const std::vector<const double *> &point_coords,
        const std::vector<std::size_t> &point_counts,
        std::size_t dimensions,
        int precision,
        Workspace *workspace = nullptr)
    {
        Workspace local;
        Workspace &ws = (workspace != nullptr) ? *workspace : local;
        return ws.encode_multipoint(point_coords, point_counts, dimensions, precision);
    }

    inline std::string encode_multilinestring(
        const std::vector<const double *> &linestring_coords,
        const std::vector<std::size_t> &linestring_point_counts,
        std::size_t dimensions,
        int precision,
        Workspace *workspace = nullptr)
    {
        Workspace local;
        Workspace &ws = (workspace != nullptr) ? *workspace : local;
        return ws.encode_multilinestring(linestring_coords, linestring_point_counts, dimensions, precision);
    }

    inline std::string encode_multipolygon(
        const std::vector<std::vector<const double *>> &polygon_ring_coords,
        const std::vector<std::vector<std::size_t>> &polygon_ring_point_counts,
        std::size_t dimensions,
        int precision,
        Workspace *workspace = nullptr)
    {
        Workspace local;
        Workspace &ws = (workspace != nullptr) ? *workspace : local;
        return ws.encode_multipolygon(polygon_ring_coords, polygon_ring_point_counts, dimensions, precision);
    }

    inline GeometryFrame decode(std::string_view encoded, Workspace *workspace = nullptr)
    {
        if (workspace == nullptr)
        {
            return decode_geometry_frame(encoded);
        }

        const GeometryHeader header = decode_geometry_header(encoded);
        const std::size_t body_start = 8;
        const std::size_t body_end = encoded.size();
        const std::size_t dims = static_cast<std::size_t>(header.dimensions);
        const std::vector<int> precisions{header.precision};

        auto split_ranges_local = [&](std::size_t start, std::size_t end, char separator)
        {
            std::vector<std::pair<std::size_t, std::size_t>> ranges;
            if (end <= start)
            {
                return ranges;
            }
            std::size_t segment_start = start;
            for (std::size_t i = start; i < end; ++i)
            {
                if (encoded[i] == separator)
                {
                    if (i <= segment_start)
                    {
                        throw std::invalid_argument("Malformed encoded geometry segment");
                    }
                    ranges.push_back({segment_start, i});
                    segment_start = i + 1;
                }
            }
            if (segment_start >= end)
            {
                throw std::invalid_argument("Malformed encoded geometry segment");
            }
            ranges.push_back({segment_start, end});
            return ranges;
        };

        auto decode_segment = [&](std::size_t start, std::size_t end)
        {
            if (end <= start)
            {
                throw std::invalid_argument("Malformed encoded geometry segment");
            }
            return workspace->decode_f64(encoded.substr(start, end - start), dims, precisions);
        };

        GeometryFrame frame{header, {}};
        switch (static_cast<EncodedGeometryType>(header.geometry_type))
        {
        case EncodedGeometryType::POINT:
        case EncodedGeometryType::LINESTRING:
            frame.groups.push_back({decode_segment(body_start, body_end)});
            break;
        case EncodedGeometryType::POLYGON:
        {
            std::vector<std::vector<double>> rings;
            for (const auto &r : split_ranges_local(body_start, body_end, ','))
            {
                rings.push_back(decode_segment(r.first, r.second));
            }
            frame.groups.push_back(std::move(rings));
            break;
        }
        case EncodedGeometryType::MULTIPOINT:
        case EncodedGeometryType::MULTILINESTRING:
            for (const auto &r : split_ranges_local(body_start, body_end, ';'))
            {
                frame.groups.push_back({decode_segment(r.first, r.second)});
            }
            break;
        case EncodedGeometryType::MULTIPOLYGON:
            for (const auto &poly : split_ranges_local(body_start, body_end, ';'))
            {
                std::vector<std::vector<double>> rings;
                for (const auto &ring : split_ranges_local(poly.first, poly.second, ','))
                {
                    rings.push_back(decode_segment(ring.first, ring.second));
                }
                frame.groups.push_back(std::move(rings));
            }
            break;
        default:
            throw std::invalid_argument("Unsupported geometry type in header");
        }
        return frame;
    }

} // namespace wkp::core
