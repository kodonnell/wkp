#include "wkp/core.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
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

    TEST_CASE("c api roundtrip")
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

        char error[256] = {0};

        wkp_workspace *workspace = nullptr;
        auto ws_status = wkp_workspace_create(0, 0, -1, -1, &workspace, error, sizeof(error));
        INFO(error);
        REQUIRE(ws_status == WKP_STATUS_OK);

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        const wkp_status s1 = wkp_workspace_encode_f64(
            workspace,
            values.data(),
            values.size(),
            3,
            precisions,
            3,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s1 == WKP_STATUS_OK);
        REQUIRE(encoded_data != nullptr);
        REQUIRE(encoded_size > 0);

        const double *decoded_data = nullptr;
        size_t decoded_size = 0;
        const wkp_status s2 = wkp_workspace_decode_f64(
            workspace,
            encoded_data,
            encoded_size,
            3,
            precisions,
            3,
            &decoded_data,
            &decoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s2 == WKP_STATUS_OK);
        REQUIRE(decoded_size == values.size());

        for (std::size_t i = 0; i < values.size(); ++i)
        {
            CHECK(almost_equal(decoded_data[i], values[i], 1e-9));
        }

        wkp_workspace_destroy(workspace);
    }

    TEST_CASE("c api encode/decode reuse working buffers")
    {
        const std::vector<double> small_values = {
            0.1,
            0.2,
            1.1,
            1.2,
        };
        const std::vector<double> large_values = {
            0.1,
            0.2,
            1.1,
            1.2,
            2.1,
            2.2,
            3.1,
            3.2,
            4.1,
            4.2,
            5.1,
            5.2,
        };
        const int precisions[] = {5, 5};
        char error[256] = {0};

        wkp_workspace *workspace = nullptr;
        auto s = wkp_workspace_create(
            0,
            0,
            -1,
            -1,
            &workspace,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(workspace != nullptr);

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        s = wkp_workspace_encode_f64(
            workspace,
            small_values.data(),
            small_values.size(),
            2,
            precisions,
            2,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(encoded_data != nullptr);
        REQUIRE(encoded_size > 0);

        s = wkp_workspace_encode_f64(
            workspace,
            large_values.data(),
            large_values.size(),
            2,
            precisions,
            2,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(encoded_data != nullptr);
        auto *encoded_ptr_after_large = encoded_data;

        const double *decoded_data = nullptr;
        size_t decoded_size = 0;
        s = wkp_workspace_decode_f64(
            workspace,
            encoded_data,
            encoded_size,
            2,
            precisions,
            2,
            &decoded_data,
            &decoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(decoded_size == large_values.size());
        auto *decoded_ptr_after_large = decoded_data;
        REQUIRE(decoded_ptr_after_large != nullptr);

        s = wkp_workspace_encode_f64(
            workspace,
            small_values.data(),
            small_values.size(),
            2,
            precisions,
            2,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        CHECK(encoded_data == encoded_ptr_after_large);

        s = wkp_workspace_decode_f64(
            workspace,
            encoded_data,
            encoded_size,
            2,
            precisions,
            2,
            &decoded_data,
            &decoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        CHECK(decoded_data == decoded_ptr_after_large);

        wkp_workspace_destroy(workspace);
    }

    TEST_CASE("c api workspace max size limit")
    {
        const std::vector<double> values = {
            0.1,
            0.2,
            1.1,
            1.2,
            2.1,
            2.2,
            3.1,
            3.2,
            4.1,
            4.2,
            5.1,
            5.2,
        };
        const int precisions[] = {5, 5};
        char error[256] = {0};

        wkp_workspace *workspace = nullptr;
        auto s = wkp_workspace_create(
            8,
            1,
            16,
            2,
            &workspace,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(workspace != nullptr);

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        s = wkp_workspace_encode_f64(
            workspace,
            values.data(),
            values.size(),
            2,
            precisions,
            2,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_LIMIT_EXCEEDED);

        wkp_workspace_destroy(workspace);
    }

    TEST_CASE("c api workspace geometry encode linestring")
    {
        const std::vector<double> coords = {
            175.26025,
            -37.79209,
            175.26026,
            -37.79210,
            175.26027,
            -37.79211,
        };
        char error[256] = {0};

        wkp_workspace *workspace = nullptr;
        auto s = wkp_workspace_create(
            2,
            2,
            -1,
            -1,
            &workspace,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        const size_t group_segment_counts[] = {1};
        const size_t segment_point_counts[] = {coords.size() / 2};
        s = wkp_workspace_encode_geometry_frame_f64(
            workspace,
            WKP_GEOMETRY_LINESTRING,
            coords.data(),
            coords.size(),
            2,
            5,
            group_segment_counts,
            1,
            segment_point_counts,
            1,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);
        REQUIRE(encoded_data != nullptr);
        REQUIRE(encoded_size >= 4);

        const std::string encoded_string(reinterpret_cast<const char *>(encoded_data), encoded_size);
        CHECK(parse_header_field(encoded_string, 0) == 1);
        CHECK(parse_header_field(encoded_string, 1) == 5);
        CHECK(parse_header_field(encoded_string, 2) == 2);
        CHECK(parse_header_field(encoded_string, 3) == WKP_GEOMETRY_LINESTRING);

        wkp_workspace_destroy(workspace);
    }

    TEST_CASE("c api workspace geometry max size limit")
    {
        const std::vector<double> coords = {
            175.26025,
            -37.79209,
            175.26026,
            -37.79210,
            175.26027,
            -37.79211,
        };
        char error[256] = {0};

        wkp_workspace *workspace = nullptr;
        auto s = wkp_workspace_create(
            1,
            1,
            4,
            -1,
            &workspace,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        const size_t group_segment_counts[] = {1};
        const size_t segment_point_counts[] = {coords.size() / 2};
        s = wkp_workspace_encode_geometry_frame_f64(
            workspace,
            WKP_GEOMETRY_LINESTRING,
            coords.data(),
            coords.size(),
            2,
            5,
            group_segment_counts,
            1,
            segment_point_counts,
            1,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_LIMIT_EXCEEDED);

        wkp_workspace_destroy(workspace);
    }

    TEST_CASE("c api repeated alloc free")
    {
        const std::vector<double> values = {
            0.1,
            0.2,
            1.1,
            1.2,
            2.1,
            2.2,
        };
        const int precisions[] = {5, 5};
        char error[256] = {0};

        for (int i = 0; i < 100; ++i)
        {
            wkp_workspace *workspace = nullptr;
            auto ws_status = wkp_workspace_create(0, 0, -1, -1, &workspace, error, sizeof(error));
            REQUIRE(ws_status == WKP_STATUS_OK);

            const uint8_t *encoded_data = nullptr;
            size_t encoded_size = 0;
            const auto s1 = wkp_workspace_encode_f64(
                workspace,
                values.data(),
                values.size(),
                2,
                precisions,
                2,
                &encoded_data,
                &encoded_size,
                error,
                sizeof(error));
            REQUIRE(s1 == WKP_STATUS_OK);

            const double *decoded_data = nullptr;
            size_t decoded_size = 0;
            const auto s2 = wkp_workspace_decode_f64(
                workspace,
                encoded_data,
                encoded_size,
                2,
                precisions,
                2,
                &decoded_data,
                &decoded_size,
                error,
                sizeof(error));
            REQUIRE(s2 == WKP_STATUS_OK);
            REQUIRE(decoded_data != nullptr);
            REQUIRE(decoded_size == values.size());

            wkp_workspace_destroy(workspace);
        }
    }

    TEST_CASE("c api geometry encode linestring")
    {
        const std::vector<double> coords = {
            175.26025,
            -37.79209,
            175.26026,
            -37.79210,
            175.26027,
            -37.79211,
        };
        char error[256] = {0};

        wkp_workspace *workspace = nullptr;
        auto s = wkp_workspace_create(2, 2, -1, -1, &workspace, error, sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);

        const size_t group_segment_counts[] = {1};
        const size_t segment_point_counts[] = {coords.size() / 2};
        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        const wkp_status s1 = wkp_workspace_encode_geometry_frame_f64(
            workspace,
            WKP_GEOMETRY_LINESTRING,
            coords.data(),
            coords.size(),
            2,
            5,
            group_segment_counts,
            1,
            segment_point_counts,
            1,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s1 == WKP_STATUS_OK);

        const std::string encoded_string(reinterpret_cast<const char *>(encoded_data), encoded_size);
        REQUIRE(encoded_string.size() >= 4);
        CHECK(parse_header_field(encoded_string, 0) == 1);
        CHECK(parse_header_field(encoded_string, 1) == 5);
        CHECK(parse_header_field(encoded_string, 2) == 2);
        CHECK(parse_header_field(encoded_string, 3) == WKP_GEOMETRY_LINESTRING);

        const int precisions[] = {5, 5};
        const std::string payload = encoded_string.substr(4);
        const double *decoded_data = nullptr;
        size_t decoded_size = 0;
        const wkp_status s2 = wkp_workspace_decode_f64(
            workspace,
            reinterpret_cast<const uint8_t *>(payload.data()),
            payload.size(),
            2,
            precisions,
            2,
            &decoded_data,
            &decoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s2 == WKP_STATUS_OK);
        REQUIRE(decoded_size == coords.size());

        for (std::size_t i = 0; i < coords.size(); ++i)
        {
            CHECK(almost_equal(decoded_data[i], coords[i], 1e-9));
        }
        wkp_workspace_destroy(workspace);
    }

    TEST_CASE("c api geometry encode polygon")
    {
        const std::vector<double> coords = {
            0.0,
            0.0,
            4.0,
            0.0,
            4.0,
            4.0,
            0.0,
            0.0,
            1.0,
            1.0,
            2.0,
            1.0,
            1.5,
            2.0,
            1.0,
            1.0,
        };
        const size_t ring_counts[] = {4, 4};
        char error[256] = {0};

        wkp_workspace *workspace = nullptr;
        auto s = wkp_workspace_create(2, 2, -1, -1, &workspace, error, sizeof(error));
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);

        const size_t group_segment_counts[] = {2};
        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        const wkp_status s_encode = wkp_workspace_encode_geometry_frame_f64(
            workspace,
            WKP_GEOMETRY_POLYGON,
            coords.data(),
            coords.size(),
            2,
            5,
            group_segment_counts,
            1,
            ring_counts,
            2,
            &encoded_data,
            &encoded_size,
            error,
            sizeof(error));
        INFO(error);
        REQUIRE(s_encode == WKP_STATUS_OK);

        const std::string encoded_string(reinterpret_cast<const char *>(encoded_data), encoded_size);
        REQUIRE(encoded_string.size() >= 4);
        CHECK(parse_header_field(encoded_string, 3) == WKP_GEOMETRY_POLYGON);
        CHECK(encoded_string.find(',') != std::string::npos);

        wkp_workspace_destroy(workspace);
    }

} // namespace
