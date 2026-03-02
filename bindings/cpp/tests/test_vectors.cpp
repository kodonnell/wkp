#include "wkp/core.hpp"

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
}

TEST_CASE("known google polyline")
{
    const std::vector<double> values = {
        38.5,
        -120.2,
        40.7,
        -120.95,
        43.252,
        -126.453,
    };
    const std::string expected = "_p~iF~ps|U_ulLnnqC_mqNvxq`@";
    std::string encoded;
    wkp::core::encode_f64_into(values.data(), values.size(), 2, {5}, encoded);
    CHECK(encoded == expected);
}

TEST_CASE("known 3d vector")
{
    const std::vector<double> values = {
        1.0,
        1.1,
        1.2,
        2.1,
        2.2,
        2.3,
    };
    const std::string expected = "o}@wcA_jAwcAwcAwcA";
    std::string encoded;
    wkp::core::encode_f64_into(values.data(), values.size(), 3, {3, 3, 3}, encoded);
    CHECK(encoded == expected);
}

TEST_CASE("known 2d vector case 1")
{
    const std::vector<double> values = {
        4.712723,
        7.846801,
        36.651759,
        9.693021,
    };
    const std::string expected = "omw[oq{n@_b}aE{qgJ";
    std::string encoded;
    wkp::core::encode_f64_into(values.data(), values.size(), 2, {5, 5}, encoded);
    CHECK(encoded == expected);
}

TEST_CASE("known 3d vector case 2")
{
    const std::vector<double> values = {
        1.0,
        1.1,
        1.2,
        2.1,
        2.2,
        2.3,
    };
    const std::string expected = "o}@wcA_jAwcAwcAwcA";
    std::string encoded;
    wkp::core::encode_f64_into(values.data(), values.size(), 3, {3}, encoded);
    CHECK(encoded == expected);
}

TEST_CASE("roundtrip 2d")
{
    const std::vector<double> values = {
        -0.1,
        0.2,
        1.12345,
        -5.67891,
        100.55555,
        -200.44444,
    };
    std::string encoded;
    wkp::core::encode_f64_into(values.data(), values.size(), 2, {5}, encoded);
    std::vector<double> decoded;
    wkp::core::decode_f64_into(encoded, 2, {5}, decoded);
    REQUIRE(decoded.size() == values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        CHECK(almost_equal(values[i], decoded[i], 1e-9));
    }
}

TEST_CASE("roundtrip 3d mixed precision")
{
    const std::vector<double> values = {
        175.26025,
        -37.79209,
        1677818753.0,
        175.26026,
        -37.79210,
        1677818754.0,
    };
    const std::vector<int> precisions = {6, 6, 0};
    std::string encoded;
    wkp::core::encode_f64_into(values.data(), values.size(), 3, precisions, encoded);
    std::vector<double> decoded;
    wkp::core::decode_f64_into(encoded, 3, precisions, decoded);
    REQUIRE(decoded.size() == values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        CHECK(almost_equal(values[i], decoded[i], 1e-9));
    }
}

TEST_CASE("roundtrip 3d multi values mixed precision")
{
    const std::vector<double> values = {
        -125.12,
        999.123,
        1.2345,
        0.0,
        1.001,
        2.0001,
        250.34,
        0.555,
        999.9999,
        -0.01,
        10.111,
        20.2222,
        45.67,
        89.101,
        123.4567,
    };

    const std::vector<int> precisions = {2, 3, 4};
    std::string encoded;
    wkp::core::encode_f64_into(values.data(), values.size(), 3, precisions, encoded);
    std::vector<double> decoded;
    wkp::core::decode_f64_into(encoded, 3, precisions, decoded);

    REQUIRE(decoded.size() == values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        CHECK(almost_equal(values[i], decoded[i], 1e-9));
    }
}

TEST_CASE("single precision expansion")
{
    const std::vector<int> p = wkp::core::normalize_precisions(4, {3});
    REQUIRE(p.size() == 4);
    CHECK(p[0] == 3);
    CHECK(p[1] == 3);
    CHECK(p[2] == 3);
    CHECK(p[3] == 3);
}
