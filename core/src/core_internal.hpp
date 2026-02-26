#pragma once

#include <cstddef>
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
