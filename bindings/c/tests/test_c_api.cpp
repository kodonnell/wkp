#include "wkp/core.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{

    bool almost_equal(double a, double b, double eps = 1e-12)
    {
        return std::fabs(a - b) <= eps;
    }

    int parse_header_field(const std::string &encoded, std::size_t offset)
    {
        return static_cast<int>(static_cast<unsigned char>(encoded[offset])) - 63;
    }

    TEST_CASE("c api f64 into roundtrip")
    {
        const std::vector<double> values = {
            175.26025,
            -37.79209,
            1677818753.0,
            175.26026,
            -37.79210,
            1677818754.0,
        };
        const int precisions[] = {6, 6, 0};
        wkp_context ctx{};
        auto s = wkp_context_init(&ctx);
        REQUIRE(s == WKP_STATUS_OK);

        const uint8_t *encoded = nullptr;
        size_t encoded_size = 0;
        s = wkp_encode_f64(
            &ctx,
            values.data(), values.size(), 3,
            precisions, 3,
            &encoded, &encoded_size);
        REQUIRE(s == WKP_STATUS_OK);

        const double *decoded = nullptr;
        size_t decoded_size = 0;
        s = wkp_decode_f64(
            &ctx,
            encoded, encoded_size,
            3,
            precisions, 3,
            &decoded, &decoded_size);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(decoded_size == values.size());

        for (std::size_t i = 0; i < values.size(); ++i)
        {
            CHECK(almost_equal(decoded[i], values[i], 1e-9));
        }

        wkp_context_free(&ctx);
    }

    TEST_CASE("c api run self-tests")
    {
        int failed_check = 0;
        const auto s = wkp_basic_self_test(&failed_check);
        CHECK(s == WKP_STATUS_OK);
        CHECK(failed_check == 0);
    }

    TEST_CASE("c api f64 auto wrappers")
    {
        const std::vector<double> values = {38.5, -120.2, 40.7, -120.95, 43.252, -126.453};
        const int precisions[] = {5};
        wkp_context ctx{};
        auto s = wkp_context_init(&ctx);
        REQUIRE(s == WKP_STATUS_OK);

        const uint8_t *encoded = nullptr;
        size_t encoded_size = 0;
        s = wkp_encode_f64(
            &ctx,
            values.data(), values.size(), 2,
            precisions, 1,
            &encoded, &encoded_size);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(encoded != nullptr);

        const double *decoded = nullptr;
        size_t decoded_size = 0;
        s = wkp_decode_f64(
            &ctx,
            encoded, encoded_size,
            2,
            precisions, 1,
            &decoded, &decoded_size);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(decoded != nullptr);
        REQUIRE(decoded_size == values.size());

        wkp_context_free(&ctx);
    }

    TEST_CASE("c api geometry frame encode/decode")
    {
        const std::vector<double> coords = {
            175.26025,
            -37.79209,
            175.26026,
            -37.79210,
            175.26027,
            -37.79211,
        };
        const size_t group_segment_counts[] = {1};
        const size_t segment_point_counts[] = {3};
        wkp_context ctx{};
        auto s = wkp_context_init(&ctx);
        REQUIRE(s == WKP_STATUS_OK);

        const uint8_t *encoded = nullptr;
        size_t encoded_size = 0;
        s = wkp_encode_geometry_frame(
            &ctx,
            WKP_GEOMETRY_LINESTRING,
            coords.data(), coords.size(),
            2, 5,
            group_segment_counts, 1,
            segment_point_counts, 1,
            &encoded, &encoded_size);
        REQUIRE(s == WKP_STATUS_OK);

        std::string encoded_string(reinterpret_cast<const char *>(encoded), encoded_size);
        CHECK(parse_header_field(encoded_string, 0) == 1);
        CHECK(parse_header_field(encoded_string, 1) == 5);
        CHECK(parse_header_field(encoded_string, 2) == 2);
        CHECK(parse_header_field(encoded_string, 3) == WKP_GEOMETRY_LINESTRING);

        const wkp_geometry_frame_f64 *frame = nullptr;
        s = wkp_decode_geometry_frame(&ctx, encoded, encoded_size, &frame);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(frame != nullptr);
        REQUIRE(frame->version == 1);
        REQUIRE(frame->precision == 5);
        REQUIRE(frame->dimensions == 2);
        REQUIRE(frame->geometry_type == WKP_GEOMETRY_LINESTRING);
        REQUIRE(frame->coord_value_count == coords.size());

        wkp_context_free(&ctx);
    }

    TEST_CASE("c api geometry frame auto wrapper")
    {
        const std::vector<double> coords = {
            175.26025,
            -37.79209,
            175.26026,
            -37.79210,
            175.26027,
            -37.79211,
        };
        const size_t group_segment_counts[] = {1};
        const size_t segment_point_counts[] = {3};
        wkp_context ctx{};
        auto s = wkp_context_init(&ctx);
        REQUIRE(s == WKP_STATUS_OK);

        const uint8_t *encoded = nullptr;
        size_t encoded_size = 0;
        s = wkp_encode_geometry_frame(
            &ctx,
            WKP_GEOMETRY_LINESTRING,
            coords.data(), coords.size(),
            2, 5,
            group_segment_counts, 1,
            segment_point_counts, 1,
            &encoded, &encoded_size);
        REQUIRE(s == WKP_STATUS_OK);

        const wkp_geometry_frame_f64 *frame = nullptr;
        s = wkp_decode_geometry_frame(
            &ctx,
            encoded, encoded_size,
            &frame);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(frame != nullptr);
        CHECK(frame->version == 1);
        CHECK(frame->precision == 5);
        CHECK(frame->dimensions == 2);
        CHECK(frame->geometry_type == WKP_GEOMETRY_LINESTRING);
        CHECK(frame->coord_value_count == coords.size());

        wkp_context_free(&ctx);
    }

} // namespace
