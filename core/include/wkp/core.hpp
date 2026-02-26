#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wkp::core
{

    std::string_view version() noexcept;

    enum class EncodedGeometryType
    {
        POINT = 1,
        LINESTRING = 2,
        POLYGON = 3,
        MULTIPOINT = 4,
        MULTILINESTRING = 5,
        MULTIPOLYGON = 6,
    };

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
        GeometryEncoder(int precision, int dimensions, std::size_t initial_capacity = 4096);

        int precision() const noexcept;
        int dimensions() const noexcept;

        std::string encode_point(const double *coords, std::size_t point_count);
        std::string encode_linestring(const double *coords, std::size_t point_count);

        std::string encode_polygon(
            const std::vector<const double *> &ring_coords,
            const std::vector<std::size_t> &ring_point_counts);

        std::string encode_multipoint(
            const std::vector<const double *> &point_coords,
            const std::vector<std::size_t> &point_counts);

        std::string encode_multilinestring(
            const std::vector<const double *> &linestring_coords,
            const std::vector<std::size_t> &linestring_point_counts);

        std::string encode_multipolygon(
            const std::vector<std::vector<const double *>> &polygon_ring_coords,
            const std::vector<std::vector<std::size_t>> &polygon_ring_point_counts);

    private:
        int precision_;
        int dimensions_;
        std::vector<double> factors_;
        std::string header_prefix_;
        std::string work_;

        void reset_with_header(EncodedGeometryType geometry_type);
        void append_separator(char sep);
        void append_coords_segment(const double *coords, std::size_t point_count);
    };

    std::string encode_f64(
        const double *values,
        std::size_t value_count,
        std::size_t dimensions,
        const std::vector<int> &precisions);

    std::string encode_f64(
        const std::vector<double> &values,
        std::size_t dimensions,
        const std::vector<int> &precisions);

    std::vector<double> decode_f64(
        std::string_view encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions);

} // namespace wkp::core
