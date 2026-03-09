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
            const std::vector<double> values = {1.0, 2.0};
            std::string encoded;
            wkp::core::encode_f64_into(values.data(), values.size(), 0, {5}, encoded);
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
            const std::vector<double> values = {1.0, 2.0, 3.0, 4.0};
            std::string encoded;
            wkp::core::encode_f64_into(values.data(), values.size(), 2, {1, 2, 3}, encoded);
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
            std::vector<double> decoded;
            wkp::core::decode_f64_into("_p~iF~ps|", 2, {5}, decoded);
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
        const uint8_t *out_encoded = nullptr;
        size_t out_encoded_size = 0;
        const auto s1 = wkp_workspace_encode_f64(nullptr, nullptr, 0, 2, nullptr, 0, &out_encoded, &out_encoded_size, error, sizeof(error));
        CHECK(s1 == WKP_STATUS_INVALID_ARGUMENT);

        wkp_workspace *workspace = nullptr;
        auto ws_status = wkp_workspace_create(0, 0, -1, -1, &workspace, error, sizeof(error));
        CHECK(ws_status == WKP_STATUS_OK);

        const uint8_t encoded[] = {'~'};
        const int precisions[] = {5, 5};
        const double *out_values = nullptr;
        size_t out_values_size = 0;
        const auto s2 = wkp_workspace_decode_f64(workspace, encoded, sizeof(encoded), 2, precisions, 2, &out_values, &out_values_size, error, sizeof(error));
        const bool decode_failed_as_expected = (s2 == WKP_STATUS_MALFORMED_INPUT || s2 == WKP_STATUS_INVALID_ARGUMENT);
        CHECK_MESSAGE(
            decode_failed_as_expected,
            "C API decode should reject invalid stream");

        wkp_workspace_destroy(workspace);
    }

} // namespace
