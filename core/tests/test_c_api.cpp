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

    int parse_2digits(const std::string &encoded, std::size_t offset)
    {
        const char a = encoded[offset];
        const char b = encoded[offset + 1];
        return (a - '0') * 10 + (b - '0');
    }

    template <typename EncodeFn>
    wkp_status encode_retry_u8(
        std::vector<uint8_t> &storage,
        wkp_u8_buffer &out,
        char *error,
        std::size_t error_capacity,
        EncodeFn &&encode_fn)
    {
        if (storage.empty())
        {
            storage.resize(64);
        }

        for (;;)
        {
            out.data = storage.data();
            out.size = storage.size();
            const wkp_status status = encode_fn(&out, error, error_capacity);
            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                storage.resize(out.size);
                continue;
            }
            return status;
        }
    }

    template <typename DecodeFn>
    wkp_status decode_retry_f64(
        std::vector<double> &storage,
        wkp_f64_buffer &out,
        char *error,
        std::size_t error_capacity,
        DecodeFn &&decode_fn)
    {
        if (storage.empty())
        {
            storage.resize(16);
        }

        for (;;)
        {
            out.data = storage.data();
            out.size = storage.size();
            const wkp_status status = decode_fn(&out, error, error_capacity);
            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                storage.resize(out.size);
                continue;
            }
            return status;
        }
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

        std::vector<uint8_t> encoded_storage;
        wkp_u8_buffer encoded{};
        char error[256] = {0};

        const wkp_status s1 = encode_retry_u8(
            encoded_storage,
            encoded,
            error,
            sizeof(error),
            [&](wkp_u8_buffer *out, char *err, std::size_t err_cap)
            {
                return wkp_encode_f64_into(
                    values.data(),
                    values.size(),
                    3,
                    precisions,
                    3,
                    out,
                    err,
                    err_cap);
            });
        INFO(error);
        REQUIRE(s1 == WKP_STATUS_OK);
        REQUIRE(encoded.data != nullptr);
        REQUIRE(encoded.size > 0);

        std::vector<double> decoded_storage;
        wkp_f64_buffer decoded{};
        const wkp_status s2 = decode_retry_f64(
            decoded_storage,
            decoded,
            error,
            sizeof(error),
            [&](wkp_f64_buffer *out, char *err, std::size_t err_cap)
            {
                return wkp_decode_f64_into(
                    encoded.data,
                    encoded.size,
                    3,
                    precisions,
                    3,
                    out,
                    err,
                    err_cap);
            });
        INFO(error);
        REQUIRE(s2 == WKP_STATUS_OK);
        REQUIRE(decoded.size == values.size());

        for (std::size_t i = 0; i < values.size(); ++i)
        {
            CHECK(almost_equal(decoded.data[i], values[i], 1e-9));
        }

        wkp_free_u8_buffer(&encoded);
        wkp_free_f64_buffer(&decoded);

        CHECK(encoded.data == nullptr);
        CHECK(encoded.size == 0);
        CHECK(decoded.data == nullptr);
        CHECK(decoded.size == 0);
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
            std::vector<uint8_t> encoded_storage;
            wkp_u8_buffer encoded{};
            std::vector<double> decoded_storage;
            wkp_f64_buffer decoded{};

            const auto s1 = encode_retry_u8(
                encoded_storage,
                encoded,
                error,
                sizeof(error),
                [&](wkp_u8_buffer *out, char *err, std::size_t err_cap)
                {
                    return wkp_encode_f64_into(values.data(), values.size(), 2, precisions, 2, out, err, err_cap);
                });
            REQUIRE(s1 == WKP_STATUS_OK);

            const auto s2 = decode_retry_f64(
                decoded_storage,
                decoded,
                error,
                sizeof(error),
                [&](wkp_f64_buffer *out, char *err, std::size_t err_cap)
                {
                    return wkp_decode_f64_into(encoded.data, encoded.size, 2, precisions, 2, out, err, err_cap);
                });
            REQUIRE(s2 == WKP_STATUS_OK);

            wkp_free_u8_buffer(&encoded);
            wkp_free_f64_buffer(&decoded);
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

        std::vector<uint8_t> encoded_storage;
        wkp_u8_buffer encoded{};
        const wkp_status s1 = encode_retry_u8(
            encoded_storage,
            encoded,
            error,
            sizeof(error),
            [&](wkp_u8_buffer *out, char *err, std::size_t err_cap)
            {
                return wkp_encode_linestring_f64_into(
                    coords.data(),
                    coords.size(),
                    2,
                    5,
                    out,
                    err,
                    err_cap);
            });
        INFO(error);
        REQUIRE(s1 == WKP_STATUS_OK);

        const std::string encoded_string(reinterpret_cast<const char *>(encoded.data), encoded.size);
        REQUIRE(encoded_string.size() >= 8);
        CHECK(parse_2digits(encoded_string, 0) == 1);
        CHECK(parse_2digits(encoded_string, 2) == 5);
        CHECK(parse_2digits(encoded_string, 4) == 2);
        CHECK(parse_2digits(encoded_string, 6) == WKP_GEOMETRY_LINESTRING);

        const int precisions[] = {5, 5};
        const std::string payload = encoded_string.substr(8);
        std::vector<double> decoded_storage;
        wkp_f64_buffer decoded{};
        const wkp_status s2 = decode_retry_f64(
            decoded_storage,
            decoded,
            error,
            sizeof(error),
            [&](wkp_f64_buffer *out, char *err, std::size_t err_cap)
            {
                return wkp_decode_f64_into(
                    reinterpret_cast<const uint8_t *>(payload.data()),
                    payload.size(),
                    2,
                    precisions,
                    2,
                    out,
                    err,
                    err_cap);
            });
        INFO(error);
        REQUIRE(s2 == WKP_STATUS_OK);
        REQUIRE(decoded.size == coords.size());

        for (std::size_t i = 0; i < coords.size(); ++i)
        {
            CHECK(almost_equal(decoded.data[i], coords[i], 1e-9));
        }

        wkp_free_u8_buffer(&encoded);
        wkp_free_f64_buffer(&decoded);
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

        std::vector<uint8_t> encoded_storage;
        wkp_u8_buffer encoded{};
        const wkp_status s = encode_retry_u8(
            encoded_storage,
            encoded,
            error,
            sizeof(error),
            [&](wkp_u8_buffer *out, char *err, std::size_t err_cap)
            {
                return wkp_encode_polygon_f64_into(
                    coords.data(),
                    coords.size(),
                    2,
                    5,
                    ring_counts,
                    2,
                    out,
                    err,
                    err_cap);
            });
        INFO(error);
        REQUIRE(s == WKP_STATUS_OK);

        const std::string encoded_string(reinterpret_cast<const char *>(encoded.data), encoded.size);
        REQUIRE(encoded_string.size() >= 8);
        CHECK(parse_2digits(encoded_string, 6) == WKP_GEOMETRY_POLYGON);
        CHECK(encoded_string.find(',') != std::string::npos);

        wkp_free_u8_buffer(&encoded);
    }

} // namespace
