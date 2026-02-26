#include "wkp/core.hpp"

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

    void expect_segment_values(
        const std::vector<double> &actual,
        const std::vector<double> &expected,
        const std::string &message,
        double eps = 1e-9)
    {
        INFO(message);
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            CHECK(almost_equal(actual[i], expected[i], eps));
        }
    }

    TEST_CASE("point and linestring headers")
    {
        wkp::core::GeometryEncoder encoder(5, 2);

        const std::vector<double> point = {38.5, -120.2};
        const std::string point_encoded = encoder.encode_point(point.data(), 1);
        CHECK(point_encoded.rfind("01050201", 0) == 0);
        const auto point_header = wkp::core::decode_geometry_header(point_encoded);
        CHECK(point_header.version == 1);
        CHECK(point_header.precision == 5);
        CHECK(point_header.dimensions == 2);
        CHECK(point_header.geometry_type == static_cast<int>(wkp::core::EncodedGeometryType::POINT));

        const std::vector<double> line = {
            38.5,
            -120.2,
            40.7,
            -120.95,
            43.252,
            -126.453,
        };
        const std::string line_expected = "01050202_p~iF~ps|U_ulLnnqC_mqNvxq`@";
        const std::string line_encoded = encoder.encode_linestring(line.data(), 3);
        CHECK(line_encoded == line_expected);
        const auto line_header = wkp::core::decode_geometry_header(line_encoded);
        CHECK(line_header.version == 1);
        CHECK(line_header.precision == 5);
        CHECK(line_header.dimensions == 2);
        CHECK(line_header.geometry_type == static_cast<int>(wkp::core::EncodedGeometryType::LINESTRING));
    }

    TEST_CASE("point 3d truncated precision")
    {
        wkp::core::GeometryEncoder encoder(1, 3);

        const std::vector<double> point = {174.776, -41.289, 123.4};
        const std::string encoded = encoder.encode_point(point.data(), 1);
        CHECK(encoded.rfind("01010301", 0) == 0);

        const auto frame = wkp::core::decode_geometry_frame(encoded);
        REQUIRE(frame.groups.size() == 1);
        REQUIRE(frame.groups[0].size() == 1);
        REQUIRE(frame.groups[0][0].size() == 3);
        CHECK(almost_equal(frame.groups[0][0][0], point[0], 0.1));
        CHECK(almost_equal(frame.groups[0][0][1], point[1], 0.1));
        CHECK(almost_equal(frame.groups[0][0][2], point[2], 0.1));
    }

    TEST_CASE("linestring roundtrip frame values")
    {
        wkp::core::GeometryEncoder encoder(6, 2);
        const std::vector<double> line = {
            174.776,
            -41.289,
            174.777,
            -41.290,
            174.778,
            -41.291,
        };

        const std::string encoded = encoder.encode_linestring(line.data(), 3);
        const auto frame = wkp::core::decode_geometry_frame(encoded);
        REQUIRE(frame.groups.size() == 1);
        REQUIRE(frame.groups[0].size() == 1);
        expect_segment_values(frame.groups[0][0], line, "LINESTRING roundtrip values mismatch");
    }

    TEST_CASE("polygon and multipolygon separators")
    {
        wkp::core::GeometryEncoder encoder(5, 2);

        const std::vector<double> shell = {
            0,
            0,
            10,
            0,
            10,
            10,
            0,
            10,
            0,
            0,
        };
        const std::vector<double> hole = {
            2,
            2,
            4,
            2,
            4,
            4,
            2,
            4,
            2,
            2,
        };

        const std::vector<const double *> rings = {shell.data(), hole.data()};
        const std::vector<std::size_t> ring_counts = {5, 5};
        const std::string polygon_encoded = encoder.encode_polygon(rings, ring_counts);
        CHECK(polygon_encoded.rfind("01050203", 0) == 0);
        CHECK(polygon_encoded.find(',') != std::string::npos);

        const std::vector<std::vector<const double *>> polys = {
            {shell.data()},
            {shell.data(), hole.data()},
        };
        const std::vector<std::vector<std::size_t>> poly_counts = {
            {5},
            {5, 5},
        };

        const std::string multipolygon_encoded = encoder.encode_multipolygon(polys, poly_counts);
        CHECK(multipolygon_encoded.rfind("01050206", 0) == 0);
        CHECK(multipolygon_encoded.find(';') != std::string::npos);
        CHECK(multipolygon_encoded.find(',') != std::string::npos);
    }

    TEST_CASE("multipoint and multilinestring")
    {
        wkp::core::GeometryEncoder encoder(5, 2);

        const std::vector<double> p1 = {1.0, 2.0};
        const std::vector<double> p2 = {3.0, 4.0};
        const std::vector<const double *> points = {p1.data(), p2.data()};
        const std::vector<std::size_t> point_counts = {1, 1};

        const std::string multipoint_encoded = encoder.encode_multipoint(points, point_counts);
        CHECK(multipoint_encoded.rfind("01050204", 0) == 0);
        CHECK(multipoint_encoded.find(';') != std::string::npos);

        const auto multipoint_frame = wkp::core::decode_geometry_frame(multipoint_encoded);
        REQUIRE(multipoint_frame.groups.size() == 2);
        REQUIRE(multipoint_frame.groups[0].size() == 1);
        REQUIRE(multipoint_frame.groups[1].size() == 1);
        expect_segment_values(multipoint_frame.groups[0][0], p1, "MULTIPOINT first point mismatch");
        expect_segment_values(multipoint_frame.groups[1][0], p2, "MULTIPOINT second point mismatch");

        const std::vector<double> l1 = {0, 0, 1, 1};
        const std::vector<double> l2 = {2, 2, 3, 3};
        const std::vector<const double *> lines = {l1.data(), l2.data()};
        const std::vector<std::size_t> line_counts = {2, 2};

        const std::string multilinestring_encoded = encoder.encode_multilinestring(lines, line_counts);
        CHECK(multilinestring_encoded.rfind("01050205", 0) == 0);
        CHECK(multilinestring_encoded.find(';') != std::string::npos);

        const auto multilinestring_frame = wkp::core::decode_geometry_frame(multilinestring_encoded);
        REQUIRE(multilinestring_frame.groups.size() == 2);
        REQUIRE(multilinestring_frame.groups[0].size() == 1);
        REQUIRE(multilinestring_frame.groups[1].size() == 1);
        expect_segment_values(multilinestring_frame.groups[0][0], l1, "MULTILINESTRING first line mismatch");
        expect_segment_values(multilinestring_frame.groups[1][0], l2, "MULTILINESTRING second line mismatch");
    }

    TEST_CASE("polygon roundtrip with holes")
    {
        wkp::core::GeometryEncoder encoder(6, 2);

        const std::vector<double> shell = {
            0,
            0,
            10,
            0,
            10,
            10,
            0,
            10,
            0,
            0,
        };
        const std::vector<double> hole1 = {
            2,
            2,
            4,
            2,
            4,
            4,
            2,
            4,
            2,
            2,
        };
        const std::vector<double> hole2 = {
            6,
            6,
            8,
            6,
            8,
            8,
            6,
            8,
            6,
            6,
        };

        const std::vector<const double *> rings = {shell.data(), hole1.data(), hole2.data()};
        const std::vector<std::size_t> ring_counts = {5, 5, 5};
        const auto encoded = encoder.encode_polygon(rings, ring_counts);
        const auto frame = wkp::core::decode_geometry_frame(encoded);

        REQUIRE(frame.groups.size() == 1);
        REQUIRE(frame.groups[0].size() == 3);
        expect_segment_values(frame.groups[0][0], shell, "POLYGON shell mismatch");
        expect_segment_values(frame.groups[0][1], hole1, "POLYGON hole1 mismatch");
        expect_segment_values(frame.groups[0][2], hole2, "POLYGON hole2 mismatch");
    }

    TEST_CASE("decode geometry frame shapes")
    {
        wkp::core::GeometryEncoder encoder(5, 2);

        const std::vector<double> shell = {
            0,
            0,
            10,
            0,
            10,
            10,
            0,
            10,
            0,
            0,
        };
        const std::vector<double> hole = {
            2,
            2,
            4,
            2,
            4,
            4,
            2,
            4,
            2,
            2,
        };

        const std::vector<std::vector<const double *>> polys = {
            {shell.data()},
            {shell.data(), hole.data()},
        };
        const std::vector<std::vector<std::size_t>> poly_counts = {
            {5},
            {5, 5},
        };

        const std::string encoded = encoder.encode_multipolygon(polys, poly_counts);
        const auto frame = wkp::core::decode_geometry_frame(encoded);

        CHECK(frame.header.geometry_type == static_cast<int>(wkp::core::EncodedGeometryType::MULTIPOLYGON));
        REQUIRE(frame.groups.size() == 2);
        REQUIRE(frame.groups[0].size() == 1);
        REQUIRE(frame.groups[1].size() == 2);
        REQUIRE(frame.groups[0][0].size() == 10);
        REQUIRE(frame.groups[1][1].size() == 10);
        expect_segment_values(frame.groups[0][0], shell, "MULTIPOLYGON first shell mismatch");
        expect_segment_values(frame.groups[1][1], hole, "MULTIPOLYGON hole mismatch");
    }

} // namespace
