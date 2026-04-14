#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "wkp/_version.h"
#include "wkp/core.h"

namespace nb = nanobind;

namespace
{

    using InputArray = nb::ndarray<const double, nb::ndim<2>, nb::c_contig, nb::device::cpu>;
    using OutputArray = nb::ndarray<nb::numpy, double, nb::ndim<2>, nb::c_contig, nb::device::cpu>;
    using OutputArray1D = nb::ndarray<nb::numpy, double, nb::ndim<1>, nb::c_contig, nb::device::cpu>;
    using OutputCountArray = nb::ndarray<nb::numpy, uint64_t, nb::ndim<1>, nb::c_contig, nb::device::cpu>;

    struct Context
    {
        wkp_context value{};

        Context()
        {
            const auto status = wkp_context_init(&value);
            if (status != WKP_STATUS_OK)
            {
                throw std::runtime_error("Failed to initialize WKP context");
            }
        }

        Context(const Context &) = delete;
        Context &operator=(const Context &) = delete;

        ~Context()
        {
            wkp_context_free(&value);
        }
    };

    struct F64VectorOwner
    {
        std::vector<double> values;
    };

    struct U64VectorOwner
    {
        std::vector<uint64_t> values;
    };

    struct GeometryHeader
    {
        int version;
        int precision;
        int dimensions;
        int geometry_type;
    };

    void throw_for_status(wkp_status status)
    {
        if (status == WKP_STATUS_OK)
        {
            return;
        }

        const std::string message = "WKP core error";

        if (status == WKP_STATUS_INVALID_ARGUMENT || status == WKP_STATUS_MALFORMED_INPUT)
        {
            throw std::invalid_argument(message);
        }
        if (status == WKP_STATUS_BUFFER_TOO_SMALL)
        {
            throw std::length_error("Output buffer too small");
        }
        if (status == WKP_STATUS_LIMIT_EXCEEDED)
        {
            throw std::length_error("Context max size exceeded");
        }
        throw std::runtime_error(message);
    }

    GeometryHeader parse_geometry_header(std::string_view encoded)
    {
        int version = 0;
        int precision = 0;
        int dimensions = 0;
        int geometry_type = 0;
        const auto status = wkp_decode_geometry_header(
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            &version,
            &precision,
            &dimensions,
            &geometry_type);

        throw_for_status(status);
        return GeometryHeader{version, precision, dimensions, geometry_type};
    }

    OutputArray output_array_from_flat(std::vector<double> &&flat, std::size_t dimensions)
    {
        if ((flat.size() % dimensions) != 0)
        {
            throw std::runtime_error("Decoded geometry segment has invalid length");
        }

        auto *owner = new F64VectorOwner();
        owner->values = std::move(flat);
        const std::size_t rows = owner->values.size() / dimensions;

        nb::capsule capsule(owner, [](void *p) noexcept
                            { delete static_cast<F64VectorOwner *>(p); });

        return OutputArray(owner->values.data(), {rows, dimensions}, capsule);
    }

    nb::tuple tuple_from_geometry_frame(const wkp_geometry_frame_f64 &frame)
    {
        const std::size_t dims = static_cast<std::size_t>(frame.dimensions);
        nb::list groups;

        std::size_t coord_index = 0;
        std::size_t segment_index = 0;
        for (std::size_t g = 0; g < frame.group_count; ++g)
        {
            nb::list py_group;
            const std::size_t group_segments = frame.group_segment_counts[g];
            for (std::size_t s = 0; s < group_segments; ++s)
            {
                const std::size_t point_count = frame.segment_point_counts[segment_index++];
                const std::size_t value_count = point_count * dims;

                std::vector<double> flat;
                flat.assign(frame.coords + coord_index, frame.coords + coord_index + value_count);
                coord_index += value_count;

                py_group.append(output_array_from_flat(std::move(flat), dims));
            }
            groups.append(py_group);
        }

        return nb::make_tuple(
            frame.version,
            frame.precision,
            frame.dimensions,
            frame.geometry_type,
            groups);
    }

    // Returns a flat tuple: (version, precision, dimensions, geometry_type, coords_2d, segment_point_counts, group_segment_counts)
    // coords_2d is shape (total_points, dimensions) float64
    // segment_point_counts and group_segment_counts are 1D uint64 arrays
    nb::tuple flat_tuple_from_geometry_frame(const wkp_geometry_frame_f64 &frame)
    {
        const std::size_t dims = static_cast<std::size_t>(frame.dimensions);
        const std::size_t total_points = (dims > 0) ? (frame.coord_value_count / dims) : 0;

        // Flat coords as 2D numpy array (total_points x dims)
        auto *coords_owner = new F64VectorOwner();
        coords_owner->values.assign(frame.coords, frame.coords + frame.coord_value_count);
        nb::capsule coords_cap(coords_owner, [](void *p) noexcept
                               { delete static_cast<F64VectorOwner *>(p); });
        OutputArray coords_arr(coords_owner->values.data(), {total_points, dims}, coords_cap);

        // Segment point counts as 1D uint64 array
        auto *seg_owner = new U64VectorOwner();
        seg_owner->values.resize(frame.segment_count);
        for (std::size_t i = 0; i < frame.segment_count; ++i)
        {
            seg_owner->values[i] = static_cast<uint64_t>(frame.segment_point_counts[i]);
        }
        nb::capsule seg_cap(seg_owner, [](void *p) noexcept
                            { delete static_cast<U64VectorOwner *>(p); });
        OutputCountArray seg_arr(seg_owner->values.data(), {frame.segment_count}, seg_cap);

        // Group segment counts as 1D uint64 array
        auto *grp_owner = new U64VectorOwner();
        grp_owner->values.resize(frame.group_count);
        for (std::size_t i = 0; i < frame.group_count; ++i)
        {
            grp_owner->values[i] = static_cast<uint64_t>(frame.group_segment_counts[i]);
        }
        nb::capsule grp_cap(grp_owner, [](void *p) noexcept
                            { delete static_cast<U64VectorOwner *>(p); });
        OutputCountArray grp_arr(grp_owner->values.data(), {frame.group_count}, grp_cap);

        return nb::make_tuple(
            frame.version,
            frame.precision,
            frame.dimensions,
            frame.geometry_type,
            coords_arr,
            seg_arr,
            grp_arr);
    }

} // namespace

NB_MODULE(_core, m)
{
    m.doc() = "WKP nanobind core wrapper";
    m.attr("__version__") = WKP_CORE_VERSION;
    {
        int failed_check = 0;
        const auto status = wkp_basic_self_test(&failed_check);
        if (status != WKP_STATUS_OK)
        {
            throw std::runtime_error("WKP core self-test failed during module initialization (check " + std::to_string(failed_check) + ")");
        }
    }

    m.def("core_version", []()
          { return std::string(WKP_CORE_VERSION); });

    m.def("run_self_test", []()
          {
              int failed_check = 0;
              const auto status = wkp_basic_self_test(&failed_check);
              if (status != WKP_STATUS_OK)
              {
                  throw std::runtime_error("WKP core self-test failed (check " + std::to_string(failed_check) + ")");
              }
              return true; });

    nb::class_<Context>(m, "Context")
        .def(nb::init<>());

    nb::enum_<wkp_geometry_type>(m, "EncodedGeometryType")
        .value("POINT", WKP_GEOMETRY_POINT)
        .value("LINESTRING", WKP_GEOMETRY_LINESTRING)
        .value("POLYGON", WKP_GEOMETRY_POLYGON)
        .value("MULTIPOINT", WKP_GEOMETRY_MULTIPOINT)
        .value("MULTILINESTRING", WKP_GEOMETRY_MULTILINESTRING)
        .value("MULTIPOLYGON", WKP_GEOMETRY_MULTIPOLYGON);

    m.def(
        "decode_geometry_header",
        [](nb::bytes encoded)
        {
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            const auto header = parse_geometry_header(std::string_view(data, static_cast<std::size_t>(size)));
            return nb::make_tuple(header.version, header.precision, header.dimensions, header.geometry_type);
        },
        nb::arg("encoded"));

    m.def(
        "decode_geometry_frame",
        [](Context &ctx, nb::bytes encoded)
        {
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            const wkp_geometry_frame_f64 *frame = nullptr;
            const auto status = wkp_decode_geometry_frame(
                &ctx.value,
                reinterpret_cast<const uint8_t *>(data),
                static_cast<std::size_t>(size),
                &frame);
            throw_for_status(status);

            return tuple_from_geometry_frame(*frame);
        },
        nb::arg("ctx"),
        nb::arg("encoded"));

    // Returns flat numpy arrays for direct GeometryFrame construction.
    // Tuple: (version, precision, dimensions, geometry_type,
    //         coords_2d float64[total_points, dims],
    //         segment_point_counts uint64[segment_count],
    //         group_segment_counts uint64[group_count])
    m.def(
        "decode_geometry_frame_flat",
        [](Context &ctx, nb::bytes encoded)
        {
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            const wkp_geometry_frame_f64 *frame = nullptr;
            const auto status = wkp_decode_geometry_frame(
                &ctx.value,
                reinterpret_cast<const uint8_t *>(data),
                static_cast<std::size_t>(size),
                &frame);
            throw_for_status(status);

            return flat_tuple_from_geometry_frame(*frame);
        },
        nb::arg("ctx"),
        nb::arg("encoded"));

    m.def(
        "encode_f64",
        [](Context &ctx, InputArray values, std::size_t dimensions, const std::vector<int> &precisions)
        {
            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;

            const auto status = wkp_encode_f64(
                &ctx.value,
                static_cast<const double *>(values.data()),
                values.shape(0) * values.shape(1),
                dimensions,
                precisions.data(),
                precisions.size(),
                &encoded_data,
                &encoded_size);

            throw_for_status(status);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        },
        nb::arg("ctx"),
        nb::arg("values"),
        nb::arg("dimensions"),
        nb::arg("precisions"));

    m.def(
        "decode_f64",
        [](Context &ctx, nb::bytes encoded, std::size_t dimensions, const std::vector<int> &precisions)
        {
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            const double *decoded_data = nullptr;
            std::size_t decoded_size = 0;

            const auto status = wkp_decode_f64(
                &ctx.value,
                reinterpret_cast<const uint8_t *>(data),
                static_cast<std::size_t>(size),
                dimensions,
                precisions.data(),
                precisions.size(),
                &decoded_data,
                &decoded_size);

            throw_for_status(status);

            if ((decoded_size % dimensions) != 0)
            {
                throw std::runtime_error("Decoded output has invalid length");
            }

            auto *owner = new F64VectorOwner();
            owner->values.assign(decoded_data, decoded_data + static_cast<std::ptrdiff_t>(decoded_size));
            const std::size_t rows = owner->values.size() / dimensions;

            nb::capsule capsule(owner, [](void *p) noexcept
                                { delete static_cast<F64VectorOwner *>(p); });

            return OutputArray(owner->values.data(), {rows, dimensions}, capsule);
        },
        nb::arg("ctx"),
        nb::arg("encoded"),
        nb::arg("dimensions"),
        nb::arg("precisions"));

    m.def(
        "encode_geometry_frame",
        [](Context &ctx, int geometry_type, InputArray coords, int precision, const std::vector<std::size_t> &group_segment_counts, const std::vector<std::size_t> &segment_point_counts)
        {
            const std::size_t dimensions = coords.shape(1);
            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;

            const auto status = wkp_encode_geometry_frame(
                &ctx.value,
                geometry_type,
                static_cast<const double *>(coords.data()),
                coords.shape(0) * coords.shape(1),
                dimensions,
                precision,
                group_segment_counts.data(),
                group_segment_counts.size(),
                segment_point_counts.data(),
                segment_point_counts.size(),
                &encoded_data,
                &encoded_size);

            throw_for_status(status);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        },
        nb::arg("ctx"),
        nb::arg("geometry_type"),
        nb::arg("coords"),
        nb::arg("precision"),
        nb::arg("group_segment_counts"),
        nb::arg("segment_point_counts"));
}
