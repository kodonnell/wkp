#include "wkp/core.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    struct FloatMatrix
    {
        std::vector<double> values;
        std::size_t dimensions = 0;
    };

    struct GeometryShape
    {
        int geometry_type = 0;
        std::vector<std::vector<std::pair<double, double>>> lines;
    };

    bool almost_equal(double a, double b, double eps = 1e-9)
    {
        return std::fabs(a - b) <= eps;
    }

    std::string trim(std::string s)
    {
        const auto is_space = [](unsigned char c)
        { return std::isspace(c) != 0; };
        while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
            s.pop_back();
        return s;
    }

    std::vector<std::string> split_tabs(const std::string &line)
    {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (true)
        {
            const auto pos = line.find('\t', start);
            if (pos == std::string::npos)
            {
                out.push_back(trim(line.substr(start)));
                break;
            }
            out.push_back(trim(line.substr(start, pos - start)));
            start = pos + 1;
        }
        return out;
    }

    std::vector<double> parse_numbers(const std::string &text)
    {
        std::vector<double> out;
        const char *p = text.c_str();
        char *end = nullptr;

        while (*p != '\0')
        {
            if (!std::isdigit(static_cast<unsigned char>(*p)) && *p != '-' && *p != '+' && *p != '.')
            {
                ++p;
                continue;
            }
            const double value = std::strtod(p, &end);
            if (end == p)
            {
                ++p;
                continue;
            }
            out.push_back(value);
            p = end;
        }
        return out;
    }

    std::vector<int> parse_precisions(const std::string &text)
    {
        const auto nums = parse_numbers(text);
        std::vector<int> out;
        out.reserve(nums.size());
        for (double d : nums)
        {
            out.push_back(static_cast<int>(d));
        }
        return out;
    }

    FloatMatrix parse_float_matrix(const std::string &text)
    {
        const auto first = text.find("[[");
        if (first == std::string::npos)
        {
            throw std::runtime_error("float matrix must start with [[");
        }
        const auto first_row_end = text.find(']', first + 2);
        if (first_row_end == std::string::npos)
        {
            throw std::runtime_error("float matrix missing first row end");
        }
        const auto first_row = text.substr(first + 2, first_row_end - (first + 2));
        const auto row_nums = parse_numbers(first_row);
        if (row_nums.empty())
        {
            throw std::runtime_error("float matrix first row is empty");
        }

        FloatMatrix m;
        m.dimensions = row_nums.size();
        m.values = parse_numbers(text);
        if (m.values.size() % m.dimensions != 0)
        {
            throw std::runtime_error("float matrix has invalid shape");
        }
        return m;
    }

    std::vector<std::pair<double, double>> parse_coords_2d(const std::string &text)
    {
        const auto nums = parse_numbers(text);
        if (nums.size() % 2 != 0)
        {
            throw std::runtime_error("coordinate sequence is not 2D");
        }
        std::vector<std::pair<double, double>> coords;
        coords.reserve(nums.size() / 2);
        for (std::size_t i = 0; i < nums.size(); i += 2)
        {
            coords.emplace_back(nums[i], nums[i + 1]);
        }
        return coords;
    }

    GeometryShape parse_wkt_subset(std::string wkt)
    {
        wkt = trim(std::move(wkt));

        if (wkt.rfind("POINT", 0) == 0)
        {
            const auto open = wkt.find('(');
            const auto close = wkt.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                throw std::runtime_error("invalid POINT WKT");
            }
            GeometryShape g;
            g.geometry_type = WKP_GEOMETRY_POINT;
            g.lines.push_back(parse_coords_2d(wkt.substr(open + 1, close - open - 1)));
            if (g.lines[0].size() != 1)
            {
                throw std::runtime_error("POINT must contain exactly one coordinate");
            }
            return g;
        }

        if (wkt.rfind("LINESTRING", 0) == 0)
        {
            const auto open = wkt.find('(');
            const auto close = wkt.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                throw std::runtime_error("invalid LINESTRING WKT");
            }
            GeometryShape g;
            g.geometry_type = WKP_GEOMETRY_LINESTRING;
            g.lines.push_back(parse_coords_2d(wkt.substr(open + 1, close - open - 1)));
            return g;
        }

        if (wkt.rfind("MULTILINESTRING", 0) == 0)
        {
            const auto open = wkt.find('(');
            const auto close = wkt.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                throw std::runtime_error("invalid MULTILINESTRING WKT");
            }
            const auto body = wkt.substr(open + 1, close - open - 1);

            GeometryShape g;
            g.geometry_type = WKP_GEOMETRY_MULTILINESTRING;
            std::size_t pos = 0;
            while (pos < body.size())
            {
                const auto seg_open = body.find('(', pos);
                if (seg_open == std::string::npos)
                {
                    break;
                }
                const auto seg_close = body.find(')', seg_open + 1);
                if (seg_close == std::string::npos)
                {
                    throw std::runtime_error("invalid MULTILINESTRING segment");
                }
                g.lines.push_back(parse_coords_2d(body.substr(seg_open + 1, seg_close - seg_open - 1)));
                pos = seg_close + 1;
            }
            if (g.lines.empty())
            {
                throw std::runtime_error("MULTILINESTRING must contain at least one segment");
            }
            return g;
        }

        throw std::runtime_error("unsupported WKT for integration tests");
    }

    std::string encode_values(
        const std::vector<double> &values,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        wkp_context ctx{};
        if (wkp_context_init(&ctx) != WKP_STATUS_OK)
            throw std::runtime_error("context init failed");

        const uint8_t *data = nullptr;
        std::size_t out_size = 0;
        const auto s = wkp_encode_f64(
            &ctx,
            values.data(),
            values.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &data,
            &out_size);
        if (s != WKP_STATUS_OK)
        {
            wkp_context_free(&ctx);
            throw std::runtime_error("encode failed");
        }

        std::string out(reinterpret_cast<const char *>(data), out_size);
        wkp_context_free(&ctx);
        return out;
    }

    std::vector<double> decode_values(
        const std::string &encoded,
        std::size_t dimensions,
        const std::vector<int> &precisions)
    {
        wkp_context ctx{};
        if (wkp_context_init(&ctx) != WKP_STATUS_OK)
            throw std::runtime_error("context init failed");

        const double *decoded = nullptr;
        std::size_t decoded_size = 0;
        const auto s = wkp_decode_f64(
            &ctx,
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &decoded,
            &decoded_size);
        if (s != WKP_STATUS_OK)
        {
            wkp_context_free(&ctx);
            throw std::runtime_error("decode failed");
        }

        std::vector<double> out(decoded, decoded + decoded_size);
        wkp_context_free(&ctx);
        return out;
    }

    std::string encode_geometry(const GeometryShape &shape, int precision)
    {
        std::vector<double> flat;
        std::vector<std::size_t> segment_point_counts;
        std::vector<std::size_t> group_segment_counts;

        if (shape.geometry_type == WKP_GEOMETRY_POINT || shape.geometry_type == WKP_GEOMETRY_LINESTRING)
        {
            group_segment_counts.push_back(1);
            segment_point_counts.push_back(shape.lines[0].size());
            for (const auto &pt : shape.lines[0])
            {
                flat.push_back(pt.first);
                flat.push_back(pt.second);
            }
        }
        else if (shape.geometry_type == WKP_GEOMETRY_MULTILINESTRING)
        {
            for (const auto &line : shape.lines)
            {
                group_segment_counts.push_back(1);
                segment_point_counts.push_back(line.size());
                for (const auto &pt : line)
                {
                    flat.push_back(pt.first);
                    flat.push_back(pt.second);
                }
            }
        }
        else
        {
            throw std::runtime_error("unsupported geometry type");
        }

        wkp_context ctx{};
        if (wkp_context_init(&ctx) != WKP_STATUS_OK)
            throw std::runtime_error("context init failed");

        const uint8_t *encoded = nullptr;
        std::size_t encoded_size = 0;
        const auto s = wkp_encode_geometry_frame(
            &ctx,
            shape.geometry_type,
            flat.data(),
            flat.size(),
            2,
            precision,
            group_segment_counts.data(),
            group_segment_counts.size(),
            segment_point_counts.data(),
            segment_point_counts.size(),
            &encoded,
            &encoded_size);
        if (s != WKP_STATUS_OK)
        {
            wkp_context_free(&ctx);
            throw std::runtime_error("geometry encode failed");
        }

        std::string out(reinterpret_cast<const char *>(encoded), encoded_size);
        wkp_context_free(&ctx);
        return out;
    }

    GeometryShape decode_geometry_shape(const std::string &encoded)
    {
        wkp_context ctx{};
        if (wkp_context_init(&ctx) != WKP_STATUS_OK)
            throw std::runtime_error("context init failed");

        const wkp_geometry_frame_f64 *frame = nullptr;
        const auto s = wkp_decode_geometry_frame(
            &ctx,
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            &frame);
        if (s != WKP_STATUS_OK || frame == nullptr)
        {
            wkp_context_free(&ctx);
            throw std::runtime_error("geometry decode failed");
        }

        GeometryShape out;
        out.geometry_type = frame->geometry_type;

        std::size_t coord_index = 0;
        std::size_t segment_index = 0;

        for (std::size_t group = 0; group < frame->group_count; ++group)
        {
            const std::size_t group_segments = frame->group_segment_counts[group];
            for (std::size_t sidx = 0; sidx < group_segments; ++sidx)
            {
                const std::size_t points = frame->segment_point_counts[segment_index++];
                std::vector<std::pair<double, double>> line;
                line.reserve(points);
                for (std::size_t p = 0; p < points; ++p)
                {
                    line.emplace_back(frame->coords[coord_index], frame->coords[coord_index + 1]);
                    coord_index += 2;
                }
                out.lines.push_back(std::move(line));
            }
        }

        wkp_context_free(&ctx);
        return out;
    }

    std::vector<std::filesystem::path> fixture_files(const std::filesystem::path &dir)
    {
        std::vector<std::filesystem::path> out;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
            {
                out.push_back(entry.path());
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::filesystem::path fixture_root()
    {
#ifdef WKP_SOURCE_DIR
        return std::filesystem::path(WKP_SOURCE_DIR) / "data" / "integration_tests";
#else
        return std::filesystem::path("data") / "integration_tests";
#endif
    }
}

TEST_CASE("cpp integration floats encode fixtures")
{
    const auto base = fixture_root() / "floats" / "encode";
    for (const auto &file : fixture_files(base))
    {
        std::ifstream in(file);
        REQUIRE(in.good());
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty() || line.rfind('#', 0) == 0)
                continue;

            const auto parts = split_tabs(line);
            REQUIRE(parts.size() == 3);
            if (parts[2] == "TODO")
                continue;

            const auto precisions = parse_precisions(parts[0]);
            const auto matrix = parse_float_matrix(parts[1]);
            const auto actual = encode_values(matrix.values, matrix.dimensions, precisions);
            CHECK(actual == parts[2]);
        }
    }
}

TEST_CASE("cpp integration floats decode fixtures")
{
    const auto base = fixture_root() / "floats" / "decode";
    for (const auto &file : fixture_files(base))
    {
        std::ifstream in(file);
        REQUIRE(in.good());
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty() || line.rfind('#', 0) == 0)
                continue;

            const auto parts = split_tabs(line);
            REQUIRE(parts.size() == 3);
            if (parts[1] == "TODO" || parts[2] == "TODO")
                continue;

            const auto precisions = parse_precisions(parts[0]);
            const auto expected = parse_float_matrix(parts[2]);
            const auto dimensions = expected.dimensions;
            const auto actual = decode_values(parts[1], dimensions, precisions);

            REQUIRE(actual.size() == expected.values.size());
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                CHECK(almost_equal(actual[i], expected.values[i], 1e-12));
            }
        }
    }
}

TEST_CASE("cpp integration geometry encode fixtures")
{
    const auto base = fixture_root() / "geometry" / "encode";
    for (const auto &file : fixture_files(base))
    {
        std::ifstream in(file);
        REQUIRE(in.good());
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty() || line.rfind('#', 0) == 0)
                continue;

            const auto parts = split_tabs(line);
            REQUIRE(parts.size() == 3);
            if (parts[2] == "TODO")
                continue;

            const int precision = std::stoi(parts[0]);
            const auto shape = parse_wkt_subset(parts[1]);
            const auto actual = encode_geometry(shape, precision);
            CHECK(actual == parts[2]);
        }
    }
}

TEST_CASE("cpp integration geometry decode fixtures")
{
    const auto base = fixture_root() / "geometry" / "decode";
    for (const auto &file : fixture_files(base))
    {
        std::ifstream in(file);
        REQUIRE(in.good());
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty() || line.rfind('#', 0) == 0)
                continue;

            const auto parts = split_tabs(line);
            REQUIRE(parts.size() == 2);
            if (parts[0] == "TODO" || parts[1] == "TODO")
                continue;

            const auto expected = parse_wkt_subset(parts[1]);
            const auto actual = decode_geometry_shape(parts[0]);

            REQUIRE(actual.geometry_type == expected.geometry_type);
            REQUIRE(actual.lines.size() == expected.lines.size());
            for (std::size_t line_idx = 0; line_idx < actual.lines.size(); ++line_idx)
            {
                REQUIRE(actual.lines[line_idx].size() == expected.lines[line_idx].size());
                for (std::size_t point_idx = 0; point_idx < actual.lines[line_idx].size(); ++point_idx)
                {
                    CHECK(almost_equal(actual.lines[line_idx][point_idx].first, expected.lines[line_idx][point_idx].first));
                    CHECK(almost_equal(actual.lines[line_idx][point_idx].second, expected.lines[line_idx][point_idx].second));
                }
            }
        }
    }
}
