#include "wkp/core.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
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

    std::string encoded;
    for (int i = 0; i < warmup; ++i)
    {
        encoded = wkp::core::encode_f64(values, dimensions, precisions);
        static_cast<void>(wkp::core::decode_f64(encoded, dimensions, precisions));
    }

    double encode_ms_total = 0.0;
    for (int i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        encoded = wkp::core::encode_f64(values, dimensions, precisions);
        const auto stop = std::chrono::steady_clock::now();
        encode_ms_total += elapsed_ms(start, stop);
    }

    double decode_ms_total = 0.0;
    std::vector<double> decoded;
    for (int i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        decoded = wkp::core::decode_f64(encoded, dimensions, precisions);
        const auto stop = std::chrono::steady_clock::now();
        decode_ms_total += elapsed_ms(start, stop);
    }

    const double encode_ms = encode_ms_total / static_cast<double>(iterations);
    const double decode_ms = decode_ms_total / static_cast<double>(iterations);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "WKP C++ binding benchmark\n";
    std::cout << "points=" << point_count
              << " dimensions=" << dimensions
              << " precision=" << precision
              << " iterations=" << iterations << "\n";
    std::cout << "encoded_size_bytes=" << encoded.size() << "\n";
    std::cout << "encode_ms_avg=" << encode_ms << "\n";
    std::cout << "decode_ms_avg=" << decode_ms << "\n";
    std::cout << "total_ms_avg=" << (encode_ms + decode_ms) << "\n";
    std::cout << "encode_points_per_sec=" << (point_count / (encode_ms / 1000.0)) << "\n";
    std::cout << "decode_points_per_sec=" << (point_count / (decode_ms / 1000.0)) << "\n";

    if (decoded.size() != values.size())
    {
        std::cerr << "Decoded output size mismatch\n";
        return 1;
    }

    return 0;
}
