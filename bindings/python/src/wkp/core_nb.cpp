#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
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
    using MutableOutputArray = nb::ndarray<double, nb::ndim<2>, nb::c_contig, nb::device::cpu>;
    using MutableU8Array = nb::ndarray<uint8_t, nb::ndim<1>, nb::c_contig, nb::device::cpu>;

    struct F64VectorOwner
    {
        std::vector<double> values;
    };

    struct GeometryHeader
    {
        int version;
        int precision;
        int dimensions;
        int geometry_type;
    };

    std::vector<int> normalize_precisions(std::size_t dimensions, const std::vector<int> &precisions)
    {
        if (precisions.empty())
        {
            throw std::invalid_argument("precisions cannot be empty");
        }
        if (precisions.size() == 1)
        {
            return std::vector<int>(dimensions, precisions[0]);
        }
        if (precisions.size() != dimensions)
        {
            throw std::invalid_argument("Expected precisions.size() == dimensions or 1");
        }
        return precisions;
    }

    void throw_for_status(wkp_status status, const char *error_message)
    {
        if (status == WKP_STATUS_OK)
        {
            return;
        }

        const std::string message = (error_message != nullptr && error_message[0] != '\0')
                                        ? std::string(error_message)
                                        : std::string("WKP core error");

        if (status == WKP_STATUS_INVALID_ARGUMENT || status == WKP_STATUS_MALFORMED_INPUT)
        {
            throw std::invalid_argument(message);
        }
        if (status == WKP_STATUS_BUFFER_TOO_SMALL)
        {
            throw std::length_error(message.empty() ? "Output buffer too small" : message);
        }
        if (status == WKP_STATUS_LIMIT_EXCEEDED)
        {
            throw std::length_error(message.empty() ? "Workspace max size exceeded" : message);
        }
        throw std::runtime_error(message);
    }

    GeometryHeader parse_geometry_header(std::string_view encoded)
    {
        int version = 0;
        int precision = 0;
        int dimensions = 0;
        int geometry_type = 0;
        char error_message[512] = {0};

        const auto status = wkp_decode_geometry_header(
            reinterpret_cast<const uint8_t *>(encoded.data()),
            encoded.size(),
            &version,
            &precision,
            &dimensions,
            &geometry_type,
            error_message,
            sizeof(error_message));

        throw_for_status(status, error_message);
        return GeometryHeader{version, precision, dimensions, geometry_type};
    }

    std::vector<std::pair<std::size_t, std::size_t>> split_ranges(std::string_view encoded, std::size_t start, std::size_t end, char sep)
    {
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        std::size_t seg_start = start;
        for (std::size_t i = start; i < end; ++i)
        {
            if (encoded[i] == sep)
            {
                ranges.emplace_back(seg_start, i);
                seg_start = i + 1;
            }
        }
        ranges.emplace_back(seg_start, end);
        return ranges;
    }

    OutputArray decode_segment_to_array(
        std::string_view encoded,
        std::size_t start,
        std::size_t end,
        std::size_t dimensions,
        int precision,
        wkp_workspace *workspace)
    {
        if (end <= start)
        {
            throw std::invalid_argument("Malformed encoded geometry segment");
        }

        std::vector<int> precisions(dimensions, precision);
        std::vector<int> precisions_local = normalize_precisions(dimensions, precisions);

        char error_message[512] = {0};
        const auto *segment_ptr = reinterpret_cast<const uint8_t *>(encoded.data() + start);
        const std::size_t segment_size = end - start;
        const double *decoded_ptr = nullptr;
        std::size_t written = 0;

        if (workspace != nullptr)
        {
            const auto status = wkp_workspace_decode_f64(
                workspace,
                segment_ptr,
                segment_size,
                dimensions,
                precisions_local.data(),
                precisions_local.size(),
                &decoded_ptr,
                &written,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
        }
        else
        {
            std::vector<double> scratch(std::max<std::size_t>(dimensions, 64));
            while (true)
            {
                wkp_f64_buffer out{scratch.data(), scratch.size()};
                const auto status = wkp_decode_f64_into(
                    segment_ptr,
                    segment_size,
                    dimensions,
                    precisions_local.data(),
                    precisions_local.size(),
                    &out,
                    error_message,
                    sizeof(error_message));

                if (status == WKP_STATUS_BUFFER_TOO_SMALL)
                {
                    scratch.resize(out.size);
                    continue;
                }

                throw_for_status(status, error_message);
                written = out.size;

                auto *owner = new F64VectorOwner();
                owner->values.assign(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(written));
                const std::size_t rows = owner->values.size() / dimensions;

                nb::capsule capsule(owner, [](void *p) noexcept
                                    { delete static_cast<F64VectorOwner *>(p); });

                return OutputArray(owner->values.data(), {rows, dimensions}, capsule);
            }
        }

        if (written % dimensions != 0)
        {
            throw std::runtime_error("Decoded geometry segment has invalid length");
        }

        auto *owner = new F64VectorOwner();
        owner->values.assign(decoded_ptr, decoded_ptr + static_cast<std::ptrdiff_t>(written));
        const std::size_t rows = owner->values.size() / dimensions;

        nb::capsule capsule(owner, [](void *p) noexcept
                            { delete static_cast<F64VectorOwner *>(p); });

        return OutputArray(owner->values.data(), {rows, dimensions}, capsule);
    }

    void append_input_array(const InputArray &arr, std::size_t dimensions, const char *label, std::vector<double> &out, std::vector<std::size_t> *point_counts)
    {
        if (arr.shape(1) != dimensions)
        {
            throw std::invalid_argument(std::string(label) + " has incompatible dimensionality");
        }

        const std::size_t rows = arr.shape(0);
        const std::size_t count = rows * dimensions;
        const auto *ptr = static_cast<const double *>(arr.data());

        const std::size_t old_size = out.size();
        out.resize(old_size + count);
        std::memcpy(out.data() + old_size, ptr, count * sizeof(double));

        if (point_counts != nullptr)
        {
            point_counts->push_back(rows);
        }
    }

    class WorkspaceCore
    {
    public:
        WorkspaceCore(
            std::size_t initial_u8_capacity,
            std::size_t initial_f64_capacity,
            int64_t max_u8_size,
            int64_t max_f64_size)
        {
            char error_message[512] = {0};
            const auto status = wkp_workspace_create(
                initial_u8_capacity,
                initial_f64_capacity,
                max_u8_size,
                max_f64_size,
                &workspace_,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
            if (workspace_ == nullptr)
            {
                throw std::runtime_error("failed to create workspace");
            }
        }

        ~WorkspaceCore()
        {
            if (workspace_ != nullptr)
            {
                wkp_workspace_destroy(workspace_);
                workspace_ = nullptr;
            }
        }

        WorkspaceCore(const WorkspaceCore &) = delete;
        WorkspaceCore &operator=(const WorkspaceCore &) = delete;

        WorkspaceCore(WorkspaceCore &&other) noexcept : workspace_(other.workspace_)
        {
            other.workspace_ = nullptr;
        }

        WorkspaceCore &operator=(WorkspaceCore &&other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }
            if (workspace_ != nullptr)
            {
                wkp_workspace_destroy(workspace_);
            }
            workspace_ = other.workspace_;
            other.workspace_ = nullptr;
            return *this;
        }

        nb::bytes encode_floats(InputArray values, std::size_t dimensions, const std::vector<int> &precisions) const
        {
            const auto p = normalize_precisions(dimensions, precisions);
            if (values.shape(1) != dimensions)
            {
                throw std::invalid_argument("Input array second dimension must match 'dimensions'");
            }

            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_encode_f64(
                workspace_,
                static_cast<const double *>(values.data()),
                values.shape(0) * values.shape(1),
                dimensions,
                p.data(),
                p.size(),
                &encoded_data,
                &encoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);

            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        }

        OutputArray decode_floats(nb::bytes encoded, std::size_t dimensions, const std::vector<int> &precisions) const
        {
            const auto p = normalize_precisions(dimensions, precisions);
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            const double *decoded_data = nullptr;
            std::size_t decoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_decode_f64(
                workspace_,
                reinterpret_cast<const uint8_t *>(data),
                static_cast<std::size_t>(size),
                dimensions,
                p.data(),
                p.size(),
                &decoded_data,
                &decoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);

            if (decoded_size % dimensions != 0)
            {
                throw std::runtime_error("Decoded output has invalid length");
            }

            auto *owner = new F64VectorOwner();
            owner->values.assign(decoded_data, decoded_data + static_cast<std::ptrdiff_t>(decoded_size));
            const std::size_t rows = owner->values.size() / dimensions;

            nb::capsule capsule(owner, [](void *p) noexcept
                                { delete static_cast<F64VectorOwner *>(p); });

            return OutputArray(owner->values.data(), {rows, dimensions}, capsule);
        }

        nb::bytes encode_point(InputArray coords, int precision) const
        {
            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_encode_point_f64(
                workspace_,
                static_cast<const double *>(coords.data()),
                coords.shape(0) * coords.shape(1),
                coords.shape(1),
                precision,
                &encoded_data,
                &encoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        }

        nb::bytes encode_linestring(InputArray coords, int precision) const
        {
            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_encode_linestring_f64(
                workspace_,
                static_cast<const double *>(coords.data()),
                coords.shape(0) * coords.shape(1),
                coords.shape(1),
                precision,
                &encoded_data,
                &encoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        }

        nb::bytes encode_polygon(const std::vector<InputArray> &rings, int precision) const
        {
            if (rings.empty())
            {
                throw std::invalid_argument("polygon requires at least one ring");
            }

            const std::size_t dimensions = rings.front().shape(1);
            std::vector<double> flat;
            std::vector<std::size_t> ring_counts;
            ring_counts.reserve(rings.size());

            for (const auto &ring : rings)
            {
                append_input_array(ring, dimensions, "polygon ring", flat, &ring_counts);
            }

            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_encode_polygon_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                ring_counts.data(),
                ring_counts.size(),
                &encoded_data,
                &encoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        }

        nb::bytes encode_multipoint(const std::vector<InputArray> &points, int precision) const
        {
            if (points.empty())
            {
                throw std::invalid_argument("multipoint requires at least one point");
            }
            const std::size_t dimensions = points.front().shape(1);
            std::vector<double> flat;
            flat.reserve(points.size() * dimensions);

            for (const auto &point : points)
            {
                if (point.shape(0) != 1)
                {
                    throw std::invalid_argument("Each multipoint part must contain exactly one coordinate");
                }
                append_input_array(point, dimensions, "multipoint part", flat, nullptr);
            }

            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_encode_multipoint_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                points.size(),
                &encoded_data,
                &encoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        }

        nb::bytes encode_multilinestring(const std::vector<InputArray> &lines, int precision) const
        {
            if (lines.empty())
            {
                throw std::invalid_argument("multilinestring requires at least one linestring");
            }
            const std::size_t dimensions = lines.front().shape(1);
            std::vector<double> flat;
            std::vector<std::size_t> line_counts;
            line_counts.reserve(lines.size());

            for (const auto &line : lines)
            {
                append_input_array(line, dimensions, "multilinestring part", flat, &line_counts);
            }

            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_encode_multilinestring_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                line_counts.data(),
                line_counts.size(),
                &encoded_data,
                &encoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        }

        nb::bytes encode_multipolygon(const std::vector<std::vector<InputArray>> &polygons, int precision) const
        {
            if (polygons.empty())
            {
                throw std::invalid_argument("multipolygon requires at least one polygon");
            }
            std::vector<double> flat;
            std::vector<std::size_t> polygon_ring_counts;
            std::vector<std::size_t> ring_point_counts;

            polygon_ring_counts.reserve(polygons.size());

            for (const auto &poly : polygons)
            {
                polygon_ring_counts.push_back(poly.size());
                if (poly.empty())
                {
                    throw std::invalid_argument("Each multipolygon polygon requires at least one ring");
                }
                const std::size_t dimensions = poly.front().shape(1);
                for (const auto &ring : poly)
                {
                    append_input_array(ring, dimensions, "multipolygon ring", flat, &ring_point_counts);
                }
            }

            const std::size_t dimensions = polygons.front().front().shape(1);

            const uint8_t *encoded_data = nullptr;
            std::size_t encoded_size = 0;
            char error_message[512] = {0};
            const auto status = wkp_workspace_encode_multipolygon_f64(
                workspace_,
                flat.data(),
                flat.size(),
                dimensions,
                precision,
                polygon_ring_counts.data(),
                polygon_ring_counts.size(),
                ring_point_counts.data(),
                ring_point_counts.size(),
                &encoded_data,
                &encoded_size,
                error_message,
                sizeof(error_message));
            throw_for_status(status, error_message);
            return nb::bytes(reinterpret_cast<const char *>(encoded_data), encoded_size);
        }

        nb::tuple decode_geometry_frame(nb::bytes encoded) const
        {
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            const std::string_view input(data, static_cast<std::size_t>(size));
            const auto header = parse_geometry_header(input);
            const std::size_t dims = static_cast<std::size_t>(header.dimensions);
            const std::size_t body_start = 8;
            const std::size_t body_end = input.size();

            nb::list groups;

            switch (header.geometry_type)
            {
            case WKP_GEOMETRY_POINT:
            case WKP_GEOMETRY_LINESTRING:
            {
                nb::list py_group;
                py_group.append(decode_segment_to_array(input, body_start, body_end, dims, header.precision, workspace_));
                groups.append(py_group);
                break;
            }

            case WKP_GEOMETRY_POLYGON:
            {
                nb::list py_group;
                for (const auto &range : split_ranges(input, body_start, body_end, ','))
                {
                    py_group.append(decode_segment_to_array(input, range.first, range.second, dims, header.precision, workspace_));
                }
                groups.append(py_group);
                break;
            }

            case WKP_GEOMETRY_MULTIPOINT:
            case WKP_GEOMETRY_MULTILINESTRING:
            {
                for (const auto &range : split_ranges(input, body_start, body_end, ';'))
                {
                    nb::list py_group;
                    py_group.append(decode_segment_to_array(input, range.first, range.second, dims, header.precision, workspace_));
                    groups.append(py_group);
                }
                break;
            }

            case WKP_GEOMETRY_MULTIPOLYGON:
            {
                for (const auto &poly_range : split_ranges(input, body_start, body_end, ';'))
                {
                    nb::list py_group;
                    for (const auto &ring_range : split_ranges(input, poly_range.first, poly_range.second, ','))
                    {
                        py_group.append(decode_segment_to_array(input, ring_range.first, ring_range.second, dims, header.precision, workspace_));
                    }
                    groups.append(py_group);
                }
                break;
            }

            default:
                throw std::invalid_argument("Unsupported geometry type in header");
            }

            return nb::make_tuple(
                header.version,
                header.precision,
                header.dimensions,
                header.geometry_type,
                groups);
        }

    private:
        wkp_workspace *workspace_ = nullptr;
    };

} // namespace

NB_MODULE(_core, m)
{
    m.doc() = "WKP nanobind core wrapper";
    m.attr("__version__") = WKP_CORE_VERSION;

    m.def("core_version", []()
          { return std::string(WKP_CORE_VERSION); });

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
        [](nb::bytes encoded)
        {
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            const std::string_view input(data, static_cast<std::size_t>(size));
            const auto header = parse_geometry_header(input);
            const std::size_t dims = static_cast<std::size_t>(header.dimensions);
            const std::size_t body_start = 8;
            const std::size_t body_end = input.size();

            nb::list groups;

            switch (header.geometry_type)
            {
            case WKP_GEOMETRY_POINT:
            case WKP_GEOMETRY_LINESTRING:
            {
                nb::list py_group;
                py_group.append(decode_segment_to_array(input, body_start, body_end, dims, header.precision, nullptr));
                groups.append(py_group);
                break;
            }

            case WKP_GEOMETRY_POLYGON:
            {
                nb::list py_group;
                for (const auto &range : split_ranges(input, body_start, body_end, ','))
                {
                    py_group.append(decode_segment_to_array(input, range.first, range.second, dims, header.precision, nullptr));
                }
                groups.append(py_group);
                break;
            }

            case WKP_GEOMETRY_MULTIPOINT:
            case WKP_GEOMETRY_MULTILINESTRING:
            {
                for (const auto &range : split_ranges(input, body_start, body_end, ';'))
                {
                    nb::list py_group;
                    py_group.append(decode_segment_to_array(input, range.first, range.second, dims, header.precision, nullptr));
                    groups.append(py_group);
                }
                break;
            }

            case WKP_GEOMETRY_MULTIPOLYGON:
            {
                for (const auto &poly_range : split_ranges(input, body_start, body_end, ';'))
                {
                    nb::list py_group;
                    for (const auto &ring_range : split_ranges(input, poly_range.first, poly_range.second, ','))
                    {
                        py_group.append(decode_segment_to_array(input, ring_range.first, ring_range.second, dims, header.precision, nullptr));
                    }
                    groups.append(py_group);
                }
                break;
            }

            default:
                throw std::invalid_argument("Unsupported geometry type in header");
            }

            return nb::make_tuple(
                header.version,
                header.precision,
                header.dimensions,
                header.geometry_type,
                groups);
        },
        nb::arg("encoded"));

    nb::class_<WorkspaceCore>(m, "WorkspaceCore")
        .def(
            nb::init<std::size_t, std::size_t, int64_t, int64_t>(),
            nb::arg("initial_u8_capacity") = 4096,
            nb::arg("initial_f64_capacity") = 256,
            nb::arg("max_u8_size") = -1,
            nb::arg("max_f64_size") = -1)
        .def("encode_floats", &WorkspaceCore::encode_floats, nb::arg("values"), nb::arg("dimensions"), nb::arg("precisions"))
        .def("decode_floats", &WorkspaceCore::decode_floats, nb::arg("encoded"), nb::arg("dimensions"), nb::arg("precisions"))
        .def("encode_point", &WorkspaceCore::encode_point, nb::arg("coords"), nb::arg("precision"))
        .def("encode_linestring", &WorkspaceCore::encode_linestring, nb::arg("coords"), nb::arg("precision"))
        .def("encode_polygon", &WorkspaceCore::encode_polygon, nb::arg("rings"), nb::arg("precision"))
        .def("encode_multipoint", &WorkspaceCore::encode_multipoint, nb::arg("points"), nb::arg("precision"))
        .def("encode_multilinestring", &WorkspaceCore::encode_multilinestring, nb::arg("lines"), nb::arg("precision"))
        .def("encode_multipolygon", &WorkspaceCore::encode_multipolygon, nb::arg("polygons"), nb::arg("precision"))
        .def("decode_geometry_frame", &WorkspaceCore::decode_geometry_frame, nb::arg("encoded"));

    m.def(
        "encode_floats",
        [](InputArray values, std::size_t dimensions, const std::vector<int> &precisions)
        {
            const auto p = normalize_precisions(dimensions, precisions);
            if (values.shape(1) != dimensions)
            {
                throw std::invalid_argument("Input array second dimension must match 'dimensions'");
            }

            std::vector<uint8_t> scratch(4096);
            char error_message[512] = {0};

            while (true)
            {
                wkp_u8_buffer out{scratch.data(), scratch.size()};
                const auto status = wkp_encode_f64_into(
                    static_cast<const double *>(values.data()),
                    values.shape(0) * values.shape(1),
                    dimensions,
                    p.data(),
                    p.size(),
                    &out,
                    error_message,
                    sizeof(error_message));

                if (status == WKP_STATUS_BUFFER_TOO_SMALL)
                {
                    scratch.resize(out.size);
                    continue;
                }

                throw_for_status(status, error_message);
                return nb::bytes(reinterpret_cast<const char *>(scratch.data()), out.size);
            }
        },
        nb::arg("values"),
        nb::arg("dimensions"),
        nb::arg("precisions"));

    m.def(
        "decode_floats",
        [](nb::bytes encoded, std::size_t dimensions, const std::vector<int> &precisions)
        {
            const auto p = normalize_precisions(dimensions, precisions);
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            char error_message[512] = {0};
            std::vector<double> scratch(std::max<std::size_t>(dimensions, 64));
            std::size_t written = 0;

            while (true)
            {
                wkp_f64_buffer out{scratch.data(), scratch.size()};
                const auto status = wkp_decode_f64_into(
                    reinterpret_cast<const uint8_t *>(data),
                    static_cast<std::size_t>(size),
                    dimensions,
                    p.data(),
                    p.size(),
                    &out,
                    error_message,
                    sizeof(error_message));

                if (status == WKP_STATUS_BUFFER_TOO_SMALL)
                {
                    scratch.resize(out.size);
                    continue;
                }

                throw_for_status(status, error_message);
                written = out.size;
                break;
            }

            if (written % dimensions != 0)
            {
                throw std::runtime_error("Decoded output has invalid length");
            }

            auto *owner = new F64VectorOwner();
            owner->values.assign(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(written));
            const std::size_t rows = owner->values.size() / dimensions;

            nb::capsule capsule(owner, [](void *p) noexcept
                                { delete static_cast<F64VectorOwner *>(p); });

            return OutputArray(
                owner->values.data(),
                {rows, dimensions},
                capsule);
        },
        nb::arg("encoded"),
        nb::arg("dimensions"),
        nb::arg("precisions"));

    m.def(
        "encode_floats_into",
        [](InputArray values, std::size_t dimensions, const std::vector<int> &precisions, MutableU8Array out_buffer)
        {
            const auto p = normalize_precisions(dimensions, precisions);
            if (values.shape(1) != dimensions)
            {
                throw std::invalid_argument("Input array second dimension must match 'dimensions'");
            }

            char error_message[512] = {0};
            wkp_u8_buffer out{out_buffer.data(), out_buffer.shape(0)};
            const auto status = wkp_encode_f64_into(
                static_cast<const double *>(values.data()),
                values.shape(0) * values.shape(1),
                dimensions,
                p.data(),
                p.size(),
                &out,
                error_message,
                sizeof(error_message));

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                throw std::invalid_argument("out_buffer is too small; required bytes=" + std::to_string(out.size));
            }
            throw_for_status(status, error_message);
            return out.size;
        },
        nb::arg("values"),
        nb::arg("dimensions"),
        nb::arg("precisions"),
        nb::arg("out_buffer"));

    m.def(
        "decode_floats_into",
        [](nb::bytes encoded, std::size_t dimensions, const std::vector<int> &precisions, MutableOutputArray out_buffer)
        {
            const auto p = normalize_precisions(dimensions, precisions);
            if (out_buffer.shape(1) != dimensions)
            {
                throw std::invalid_argument("out_buffer second dimension must match 'dimensions'");
            }

            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(encoded.ptr(), &data, &size) != 0)
            {
                throw nb::python_error();
            }

            char error_message[512] = {0};
            wkp_f64_buffer out{out_buffer.data(), out_buffer.shape(0) * out_buffer.shape(1)};
            const auto status = wkp_decode_f64_into(
                reinterpret_cast<const uint8_t *>(data),
                static_cast<std::size_t>(size),
                dimensions,
                p.data(),
                p.size(),
                &out,
                error_message,
                sizeof(error_message));

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                throw std::invalid_argument("out_buffer is too small; required rows=" + std::to_string(out.size / dimensions));
            }
            throw_for_status(status, error_message);

            if (out.size % dimensions != 0)
            {
                throw std::runtime_error("Decoded output has invalid length");
            }

            const std::size_t rows = out.size / dimensions;
            return rows;
        },
        nb::arg("encoded"),
        nb::arg("dimensions"),
        nb::arg("precisions"),
        nb::arg("out_buffer"));
}
