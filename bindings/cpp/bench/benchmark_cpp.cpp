#include "wkp/core.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    std::vector<double> make_random_points(std::size_t point_count, std::size_t dimensions)
    {
        std::mt19937_64 rng(42);
        std::uniform_real_distribution<double> lon(-180.0, 180.0);
        std::uniform_real_distribution<double> lat(-90.0, 90.0);
        std::uniform_real_distribution<double> z(0.0, 5000.0);

        std::vector<double> values;
        values.reserve(point_count * dimensions);
        for (std::size_t i = 0; i < point_count; ++i)
        {
            for (std::size_t d = 0; d < dimensions; ++d)
            {
                if (d == 0)
                {
                    values.push_back(lon(rng));
                }
                else if (d == 1)
                {
                    values.push_back(lat(rng));
                }
                else
                {
                    values.push_back(z(rng));
                }
            }
        }
        return values;
    }

    double elapsed_ms(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

} // namespace

struct BenchResult
{
    std::string benchmark;
    std::size_t encoded_size_bytes = 0;
    double encode_ms = 0.0;
    double decode_ms = 0.0;
};

void throw_for_status(wkp_status status)
{
    if (status == WKP_STATUS_OK)
    {
        return;
    }
    const std::string msg = "WKP error";
    throw std::runtime_error(msg);
}

BenchResult run_bench_case(
    const std::vector<double> &values,
    std::size_t /*point_count*/,
    std::size_t dimensions,
    const std::vector<int> &precisions,
    int warmup,
    int iterations)
{
    wkp_context ctx{};
    throw_for_status(wkp_context_init(&ctx));

    auto encode_once = [&]() -> std::size_t
    {
        const uint8_t *encoded = nullptr;
        std::size_t out_size = 0;
        const auto status = wkp_encode_f64(
            &ctx,
            values.data(),
            values.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &encoded,
            &out_size);
        throw_for_status(status);
        return out_size;
    };

    auto decode_once = [&](std::size_t encoded_size) -> std::size_t
    {
        const double *decoded = nullptr;
        std::size_t out_size = 0;
        const auto status = wkp_decode_f64(
            &ctx,
            ctx.u8,
            encoded_size,
            dimensions,
            precisions.data(),
            precisions.size(),
            &decoded,
            &out_size);
        throw_for_status(status);
        (void)decoded;
        return out_size;
    };

    std::size_t encoded_size = 0;
    std::size_t decoded_size = 0;
    for (int i = 0; i < warmup; ++i)
    {
        encoded_size = encode_once();
        decoded_size = decode_once(encoded_size);
    }

    double encode_ms_total = 0.0;
    for (int i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        encoded_size = encode_once();
        const auto stop = std::chrono::steady_clock::now();
        encode_ms_total += elapsed_ms(start, stop);
    }

    double decode_ms_total = 0.0;
    for (int i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        decoded_size = decode_once(encoded_size);
        const auto stop = std::chrono::steady_clock::now();
        decode_ms_total += elapsed_ms(start, stop);
    }

    if (decoded_size != values.size())
        throw std::runtime_error("Decoded output size mismatch");

    BenchResult result;
    result.benchmark = "f64-points";
    result.encoded_size_bytes = encoded_size;
    result.encode_ms = encode_ms_total / static_cast<double>(iterations);
    result.decode_ms = decode_ms_total / static_cast<double>(iterations);
    wkp_context_free(&ctx);
    return result;
}

BenchResult run_geometry_linestring_bench_case(
    const std::vector<double> &values,
    std::size_t point_count,
    std::size_t dimensions,
    int precision,
    int warmup,
    int iterations)
{
    const std::vector<std::size_t> group_segment_counts{1};
    const std::vector<std::size_t> segment_point_counts{point_count};

    auto validate_frame = [&](int version,
                              int decoded_precision,
                              int decoded_dimensions,
                              int geometry_type,
                              std::size_t coord_value_count,
                              std::size_t decoded_segment_count,
                              std::size_t decoded_group_count,
                              const std::vector<std::size_t> &decoded_segment_points,
                              const std::vector<std::size_t> &decoded_group_segments)
    {
        if (version != 1 || decoded_precision != precision || decoded_dimensions != static_cast<int>(dimensions))
        {
            throw std::runtime_error("Decoded geometry header mismatch");
        }
        if (geometry_type != WKP_GEOMETRY_LINESTRING)
        {
            throw std::runtime_error("Decoded geometry type mismatch");
        }
        if (coord_value_count != values.size())
        {
            throw std::runtime_error("Decoded geometry coordinate count mismatch");
        }
        if (decoded_group_count != 1 || decoded_segment_count != 1)
        {
            throw std::runtime_error("Decoded geometry frame topology mismatch");
        }
        if (decoded_group_segments[0] != 1 || decoded_segment_points[0] != point_count)
        {
            throw std::runtime_error("Decoded geometry frame counts mismatch");
        }
    };

    wkp_context ctx{};
    throw_for_status(wkp_context_init(&ctx));

    auto encode_once = [&]() -> std::size_t
    {
        const uint8_t *encoded = nullptr;
        std::size_t out_size = 0;
        const auto status = wkp_encode_geometry_frame(
            &ctx,
            WKP_GEOMETRY_LINESTRING,
            values.data(),
            values.size(),
            dimensions,
            precision,
            group_segment_counts.data(),
            group_segment_counts.size(),
            segment_point_counts.data(),
            segment_point_counts.size(),
            &encoded,
            &out_size);
        throw_for_status(status);
        (void)encoded;
        return out_size;
    };

    auto decode_once = [&](std::size_t encoded_size, bool do_validate) -> std::size_t
    {
        const wkp_geometry_frame_f64 *frame = nullptr;
        const auto status = wkp_decode_geometry_frame(&ctx, ctx.u8, encoded_size, &frame);
        throw_for_status(status);
        if (frame == nullptr)
            throw std::runtime_error("Decoded geometry frame missing");

        if (do_validate)
        {
            std::vector<std::size_t> decoded_segment_points(frame->segment_point_counts, frame->segment_point_counts + frame->segment_count);
            std::vector<std::size_t> decoded_group_segments(frame->group_segment_counts, frame->group_segment_counts + frame->group_count);
            validate_frame(
                frame->version,
                frame->precision,
                frame->dimensions,
                frame->geometry_type,
                frame->coord_value_count,
                frame->segment_count,
                frame->group_count,
                decoded_segment_points,
                decoded_group_segments);
        }
        return frame->coord_value_count;
    };

    std::size_t encoded_size = 0;
    std::size_t decoded_size = 0;
    for (int i = 0; i < warmup; ++i)
    {
        encoded_size = encode_once();
        decoded_size = decode_once(encoded_size, false);
    }

    double encode_ms_total = 0.0;
    for (int i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        encoded_size = encode_once();
        const auto stop = std::chrono::steady_clock::now();
        encode_ms_total += elapsed_ms(start, stop);
    }

    double decode_ms_total = 0.0;
    for (int i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        decoded_size = decode_once(encoded_size, false);
        const auto stop = std::chrono::steady_clock::now();
        decode_ms_total += elapsed_ms(start, stop);
    }

    // Validate decoded frame once outside the timed path.
    decode_once(encoded_size, true);

    if (decoded_size != values.size())
        throw std::runtime_error("Decoded output size mismatch");

    BenchResult result;
    result.benchmark = "geometry-linestring-frame";
    result.encoded_size_bytes = encoded_size;
    result.encode_ms = encode_ms_total / static_cast<double>(iterations);
    result.decode_ms = decode_ms_total / static_cast<double>(iterations);
    wkp_context_free(&ctx);
    return result;
}

int main(int argc, char **argv)
{
    std::size_t point_count = 200000;
    std::size_t dimensions = 2;
    int precision = 5;
    int warmup = 3;
    int iterations = 20;

    if (argc > 1)
    {
        point_count = static_cast<std::size_t>(std::stoull(argv[1]));
    }
    if (argc > 2)
    {
        dimensions = static_cast<std::size_t>(std::stoull(argv[2]));
    }
    if (argc > 3)
    {
        precision = std::stoi(argv[3]);
    }
    if (argc > 4)
    {
        iterations = std::stoi(argv[4]);
    }
    const auto values = make_random_points(point_count, dimensions);
    const std::vector<int> precisions(dimensions, precision);

    std::vector<BenchResult> results;
    results.push_back(run_bench_case(values, point_count, dimensions, precisions, warmup, iterations));
    results.push_back(run_geometry_linestring_bench_case(values, point_count, dimensions, precision, warmup, iterations));

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "WKP C++ binding benchmark\n";
    std::cout << "points=" << point_count
              << " dimensions=" << dimensions
              << " precision=" << precision
              << " iterations=" << iterations << "\n";

    for (const auto &result : results)
    {
        std::cout << "--- " << result.benchmark << " ---\n";
        std::cout << "encoded_size_bytes=" << result.encoded_size_bytes << "\n";
        std::cout << "encode_ms_avg=" << result.encode_ms << "\n";
        std::cout << "decode_ms_avg=" << result.decode_ms << "\n";
        std::cout << "total_ms_avg=" << (result.encode_ms + result.decode_ms) << "\n";
        std::cout << "encode_points_per_sec=" << (point_count / (result.encode_ms / 1000.0)) << "\n";
        std::cout << "decode_points_per_sec=" << (point_count / (result.decode_ms / 1000.0)) << "\n";
    }

    return 0;
}
