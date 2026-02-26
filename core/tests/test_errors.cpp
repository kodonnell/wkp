#include "wkp/core.h"
#include "wkp/core.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    TEST_CASE("invalid dimension cpp")
    {
        bool raised = false;
        try
        {
            static_cast<void>(wkp::core::encode_f64({1.0, 2.0}, 0, {5}));
        }
        catch (const std::invalid_argument &)
        {
            raised = true;
        }
        CHECK(raised);
    }

    TEST_CASE("invalid precisions cpp")
    {
        bool raised = false;
        try
        {
            static_cast<void>(wkp::core::encode_f64({1.0, 2.0, 3.0, 4.0}, 2, {1, 2, 3}));
        }
        catch (const std::invalid_argument &)
        {
            raised = true;
        }
        CHECK(raised);
    }

    TEST_CASE("malformed decode cpp")
    {
        bool raised = false;
        try
        {
            static_cast<void>(wkp::core::decode_f64("_p~iF~ps|", 2, {5}));
        }
        catch (const std::invalid_argument &)
        {
            raised = true;
        }
        CHECK(raised);
    }

    TEST_CASE("invalid arguments c api")
    {
        char error[256] = {0};
        wkp_u8_buffer out_encoded{};

        const auto s1 = wkp_encode_f64(nullptr, 0, 2, nullptr, 0, &out_encoded, error, sizeof(error));
        CHECK(s1 == WKP_STATUS_INVALID_ARGUMENT);

        const uint8_t encoded[] = {'~'};
        const int precisions[] = {5, 5};
        wkp_f64_buffer out_values{};
        const auto s2 = wkp_decode_f64(encoded, sizeof(encoded), 2, precisions, 2, &out_values, error, sizeof(error));
        const bool decode_failed_as_expected = (s2 == WKP_STATUS_MALFORMED_INPUT || s2 == WKP_STATUS_INVALID_ARGUMENT);
        CHECK_MESSAGE(
            decode_failed_as_expected,
            "C API decode should reject invalid stream");
    }

} // namespace
