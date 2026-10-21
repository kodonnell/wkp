#include "wkp/core.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{

    bool almost_equal(double a, double b, double eps = 1e-9)
    {
        return std::fabs(a - b) <= eps;
    }

    std::string expect_header(int version, int precision, int dimensions, int geometry_type)
    {
        std::string header;
        header.push_back(static_cast<char>(version + 63));
        header.push_back(static_cast<char>(precision + 63));
        header.push_back(static_cast<char>(dimensions + 63));
        header.push_back(static_cast<char>(geometry_type + 63));
        return header;
    }

    TEST_CASE("geometry header point and linestring")
    {
        wkp_context ctx{};
        auto s = wkp_context_init(&ctx);
        REQUIRE(s == WKP_STATUS_OK);

        const std::vector<double> point = {38.5, -120.2};
        const size_t point_groups[] = {1};
        const size_t point_segments[] = {1};

        const uint8_t *point_data = nullptr;
        size_t point_size = 0;
        s = wkp_encode_geometry_frame(
            &ctx,
            WKP_GEOMETRY_POINT,
            point.data(),
            point.size(),
            2,
            5,
            point_groups,
            1,
            point_segments,
            1,
            &point_data,
            &point_size);
        REQUIRE(s == WKP_STATUS_OK);

        std::string point_encoded(reinterpret_cast<const char *>(point_data), point_size);
        CHECK(point_encoded.rfind(expect_header(1, 5, 2, 1), 0) == 0);

        const std::vector<double> line = {38.5, -120.2, 40.7, -120.95, 43.252, -126.453};
        const size_t line_groups[] = {1};
        const size_t line_segments[] = {3};

        const uint8_t *line_data = nullptr;
        size_t line_size = 0;
        s = wkp_encode_geometry_frame(
            &ctx,
            WKP_GEOMETRY_LINESTRING,
            line.data(),
            line.size(),
            2,
            5,
            line_groups,
            1,
            line_segments,
            1,
            &line_data,
            &line_size);
        REQUIRE(s == WKP_STATUS_OK);

        std::string line_encoded(reinterpret_cast<const char *>(line_data), line_size);
        CHECK(line_encoded == expect_header(1, 5, 2, 2) + "_p~iF~ps|U_ulLnnqC_mqNvxq`@");

        wkp_context_free(&ctx);
    }

    TEST_CASE("geometry frame decode linestring")
    {
        wkp_context ctx{};
        auto s = wkp_context_init(&ctx);
        REQUIRE(s == WKP_STATUS_OK);
        const std::vector<double> line = {174.776, -41.289, 174.777, -41.290, 174.778, -41.291};
        const size_t groups[] = {1};
        const size_t segments[] = {3};

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        s = wkp_encode_geometry_frame(
            &ctx,
            WKP_GEOMETRY_LINESTRING,
            line.data(),
            line.size(),
            2,
            6,
            groups,
            1,
            segments,
            1,
            &encoded_data,
            &encoded_size);
        REQUIRE(s == WKP_STATUS_OK);

        const wkp_geometry_frame_f64 *frame = nullptr;
        s = wkp_decode_geometry_frame(&ctx, encoded_data, encoded_size, &frame);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(frame != nullptr);
        REQUIRE(frame->group_count == 1);
        REQUIRE(frame->segment_count == 1);
        REQUIRE(frame->coord_value_count == line.size());

        for (std::size_t i = 0; i < line.size(); ++i)
        {
            CHECK(almost_equal(frame->coords[i], line[i], 1e-6));
        }

        wkp_context_free(&ctx);
    }

    TEST_CASE("geometry frame invalid args")
    {
        const std::vector<double> line = {174.776, -41.289, 174.777, -41.290, 174.778, -41.291};
        const size_t groups[] = {1};
        const size_t segments[] = {3};

        wkp_context ctx{};
        auto s = wkp_context_init(&ctx);
        REQUIRE(s == WKP_STATUS_OK);

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        s = wkp_encode_geometry_frame(
            &ctx,
            WKP_GEOMETRY_LINESTRING,
            line.data(),
            line.size(),
            2,
            6,
            groups,
            1,
            segments,
            1,
            &encoded_data,
            &encoded_size);

        CHECK(s == WKP_STATUS_OK);
        CHECK(encoded_data != nullptr);
        CHECK(encoded_size > 0);

        wkp_context_free(&ctx);
    }

} // namespace
