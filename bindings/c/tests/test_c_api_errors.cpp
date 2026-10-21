#include "wkp/core.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

namespace
{

    TEST_CASE("invalid dimension c api")
    {
        const std::vector<double> values = {1.0, 2.0};
        const int precisions[] = {5};
        wkp_context ctx{};
        const auto init = wkp_context_init(&ctx);
        REQUIRE(init == WKP_STATUS_OK);
        size_t out_size = 0;
        const uint8_t *out = nullptr;

        const auto s = wkp_encode_f64(
            &ctx,
            values.data(),
            values.size(),
            0,
            precisions,
            1,
            &out,
            &out_size);

        CHECK(s == WKP_STATUS_INVALID_ARGUMENT);
        wkp_context_free(&ctx);
    }

    TEST_CASE("invalid precisions c api")
    {
        const std::vector<double> values = {1.0, 2.0, 3.0, 4.0};
        const int precisions[] = {1, 2, 3};
        wkp_context ctx{};
        const auto init = wkp_context_init(&ctx);
        REQUIRE(init == WKP_STATUS_OK);
        size_t out_size = 0;
        const uint8_t *out = nullptr;

        const auto s = wkp_encode_f64(
            &ctx,
            values.data(),
            values.size(),
            2,
            precisions,
            3,
            &out,
            &out_size);

        CHECK(s == WKP_STATUS_INVALID_ARGUMENT);
        wkp_context_free(&ctx);
    }

    TEST_CASE("malformed decode c api")
    {
        const uint8_t encoded[] = {'~'};
        const int precisions[] = {5};
        wkp_context ctx{};
        const auto init = wkp_context_init(&ctx);
        REQUIRE(init == WKP_STATUS_OK);
        size_t out_size = 0;
        const double *out = nullptr;

        const auto s = wkp_decode_f64(
            &ctx,
            encoded,
            sizeof(encoded),
            2,
            precisions,
            1,
            &out,
            &out_size);

        const bool decode_failed_as_expected = (s == WKP_STATUS_MALFORMED_INPUT || s == WKP_STATUS_INVALID_ARGUMENT);
        CHECK(decode_failed_as_expected);
        wkp_context_free(&ctx);
    }

    TEST_CASE("encode wrapper succeeds c api")
    {
        const std::vector<double> values = {38.5, -120.2, 40.7, -120.95, 43.252, -126.453};
        const int precisions[] = {5};
        wkp_context ctx{};
        const auto init = wkp_context_init(&ctx);
        REQUIRE(init == WKP_STATUS_OK);
        size_t out_size = 0;
        const uint8_t *out = nullptr;

        const auto s = wkp_encode_f64(
            &ctx,
            values.data(),
            values.size(),
            2,
            precisions,
            1,
            &out,
            &out_size);

        CHECK(s == WKP_STATUS_OK);
        CHECK(out != nullptr);
        CHECK(out_size > 0);
        wkp_context_free(&ctx);
    }

} // namespace
