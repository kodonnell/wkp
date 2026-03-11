#include <napi.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "wkp/_version.h"
#include "wkp/core.h"

namespace
{

    std::vector<int> to_precisions(const Napi::Env &env, const Napi::Value &value)
    {
        if (!value.IsArray())
        {
            throw Napi::TypeError::New(env, "precisions must be an array of integers");
        }

        Napi::Array array = value.As<Napi::Array>();
        std::vector<int> out;
        out.reserve(array.Length());

        for (uint32_t i = 0; i < array.Length(); ++i)
        {
            Napi::Value item = array.Get(i);
            if (!item.IsNumber())
            {
                throw Napi::TypeError::New(env, "precisions must contain only numbers");
            }
            out.push_back(item.As<Napi::Number>().Int32Value());
        }

        return out;
    }

    std::vector<double> to_values(const Napi::Env &env, const Napi::Value &value)
    {
        std::vector<double> out;

        if (value.IsTypedArray())
        {
            Napi::TypedArray typed = value.As<Napi::TypedArray>();
            if (typed.TypedArrayType() != napi_float64_array)
            {
                throw Napi::TypeError::New(env, "values typed array must be Float64Array");
            }

            Napi::Float64Array arr = value.As<Napi::Float64Array>();
            out.assign(arr.Data(), arr.Data() + arr.ElementLength());
            return out;
        }

        if (value.IsArray())
        {
            Napi::Array array = value.As<Napi::Array>();
            out.reserve(array.Length());
            for (uint32_t i = 0; i < array.Length(); ++i)
            {
                Napi::Value item = array.Get(i);
                if (!item.IsNumber())
                {
                    throw Napi::TypeError::New(env, "values array must contain only numbers");
                }
                out.push_back(item.As<Napi::Number>().DoubleValue());
            }
            return out;
        }

        throw Napi::TypeError::New(env, "values must be Float64Array or number[]");
    }

    std::vector<uint8_t> to_encoded_bytes(const Napi::Env &env, const Napi::Value &value)
    {
        if (value.IsBuffer())
        {
            Napi::Buffer<uint8_t> buf = value.As<Napi::Buffer<uint8_t>>();
            return std::vector<uint8_t>(buf.Data(), buf.Data() + buf.Length());
        }

        if (value.IsTypedArray())
        {
            Napi::TypedArray typed = value.As<Napi::TypedArray>();
            if (typed.TypedArrayType() != napi_uint8_array)
            {
                throw Napi::TypeError::New(env, "encoded typed array must be Uint8Array");
            }
            Napi::Uint8Array arr = value.As<Napi::Uint8Array>();
            return std::vector<uint8_t>(arr.Data(), arr.Data() + arr.ElementLength());
        }

        if (value.IsString())
        {
            std::string s = value.As<Napi::String>().Utf8Value();
            return std::vector<uint8_t>(s.begin(), s.end());
        }

        throw Napi::TypeError::New(env, "encoded must be Buffer, Uint8Array, or string");
    }

    void throw_for_status(const Napi::Env &env, wkp_status status, const char *error_message)
    {
        if (status == WKP_STATUS_OK)
        {
            return;
        }

        const std::string msg = (error_message != nullptr && error_message[0] != '\0')
                                    ? std::string(error_message)
                                    : std::string("WKP core error");

        if (status == WKP_STATUS_INVALID_ARGUMENT)
        {
            throw Napi::TypeError::New(env, msg);
        }
        if (status == WKP_STATUS_BUFFER_TOO_SMALL)
        {
            throw Napi::RangeError::New(env, msg);
        }

        throw Napi::Error::New(env, msg);
    }

    Napi::Value EncodeF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 3)
        {
            throw Napi::TypeError::New(env, "encodeF64(values, dimensions, precisions) expects 3 arguments");
        }

        std::vector<double> values = to_values(env, info[0]);

        if (!info[1].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions must be a number");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());

        std::vector<int> precisions = to_precisions(env, info[2]);

        wkp_context ctx{};
        auto status = wkp_context_init(&ctx);
        throw_for_status(env, status, nullptr);

        const uint8_t *encoded_data = nullptr;
        size_t encoded_size = 0;
        status = wkp_encode_f64(
            &ctx,
            values.data(),
            values.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &encoded_data,
            &encoded_size);

        if (status != WKP_STATUS_OK)
        {
            wkp_context_free(&ctx);
            throw_for_status(env, status, nullptr);
        }

        Napi::Value out = Napi::Buffer<uint8_t>::Copy(env, encoded_data, encoded_size);
        wkp_context_free(&ctx);
        return out;
    }

    Napi::Value DecodeF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 3)
        {
            throw Napi::TypeError::New(env, "decodeF64(encoded, dimensions, precisions) expects 3 arguments");
        }

        std::vector<uint8_t> encoded = to_encoded_bytes(env, info[0]);

        if (!info[1].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions must be a number");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());

        std::vector<int> precisions = to_precisions(env, info[2]);

        wkp_context ctx{};
        auto status = wkp_context_init(&ctx);
        throw_for_status(env, status, nullptr);

        const double *decoded_data = nullptr;
        size_t decoded_size = 0;
        status = wkp_decode_f64(
            &ctx,
            encoded.data(),
            encoded.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &decoded_data,
            &decoded_size);

        if (status != WKP_STATUS_OK)
        {
            wkp_context_free(&ctx);
            throw_for_status(env, status, nullptr);
        }

        Napi::ArrayBuffer buffer = Napi::ArrayBuffer::New(env, decoded_size * sizeof(double));
        if (decoded_size > 0)
        {
            std::memcpy(buffer.Data(), decoded_data, decoded_size * sizeof(double));
        }
        wkp_context_free(&ctx);
        return Napi::Float64Array::New(env, decoded_size, buffer, 0, napi_float64_array);
    }

    Napi::Value DecodeGeometryHeader(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 1)
        {
            throw Napi::TypeError::New(env, "decodeGeometryHeader(encoded) expects 1 argument");
        }

        std::vector<uint8_t> encoded = to_encoded_bytes(env, info[0]);

        int version = 0;
        int precision = 0;
        int dimensions = 0;
        int geometry_type = 0;
        const wkp_status status = wkp_decode_geometry_header(
            encoded.data(),
            encoded.size(),
            &version,
            &precision,
            &dimensions,
            &geometry_type);

        throw_for_status(env, status, nullptr);

        Napi::Array out = Napi::Array::New(env, 4);
        out.Set(static_cast<uint32_t>(0), Napi::Number::New(env, version));
        out.Set(static_cast<uint32_t>(1), Napi::Number::New(env, precision));
        out.Set(static_cast<uint32_t>(2), Napi::Number::New(env, dimensions));
        out.Set(static_cast<uint32_t>(3), Napi::Number::New(env, geometry_type));
        return out;
    }

    std::vector<size_t> to_counts(const Napi::Env &env, const Napi::Value &value, const char *name)
    {
        if (!value.IsArray())
        {
            throw Napi::TypeError::New(env, std::string(name) + " must be an array of integers");
        }

        Napi::Array array = value.As<Napi::Array>();
        std::vector<size_t> out;
        out.reserve(array.Length());
        for (uint32_t i = 0; i < array.Length(); ++i)
        {
            Napi::Value item = array.Get(i);
            if (!item.IsNumber())
            {
                throw Napi::TypeError::New(env, std::string(name) + " must contain only numbers");
            }
            const int64_t parsed = item.As<Napi::Number>().Int64Value();
            if (parsed < 0)
            {
                throw Napi::TypeError::New(env, std::string(name) + " cannot contain negative values");
            }
            out.push_back(static_cast<size_t>(parsed));
        }
        return out;
    }

    Napi::Value encode_geometry_frame_with_workspace(
        const Napi::Env &env,
        int geometry_type,
        const std::vector<double> &coords,
        size_t dimensions,
        int precision,
        const std::vector<size_t> &group_segment_counts,
        const std::vector<size_t> &segment_point_counts)
    {
        wkp_context ctx{};
        wkp_status status = wkp_context_init(&ctx);
        throw_for_status(env, status, nullptr);

        const uint8_t *out_data = nullptr;
        size_t out_size = 0;
        status = wkp_encode_geometry_frame(
            &ctx,
            geometry_type,
            coords.data(),
            coords.size(),
            dimensions,
            precision,
            group_segment_counts.data(),
            group_segment_counts.size(),
            segment_point_counts.data(),
            segment_point_counts.size(),
            &out_data,
            &out_size);

        Napi::Value result = env.Null();
        if (status == WKP_STATUS_OK)
        {
            result = Napi::Buffer<uint8_t>::Copy(env, out_data, out_size);
        }

        wkp_context_free(&ctx);
        throw_for_status(env, status, nullptr);
        return result;
    }

    Napi::Value EncodePointF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 3)
        {
            throw Napi::TypeError::New(env, "encodePointF64(coords, dimensions, precision) expects 3 arguments");
        }

        std::vector<double> coords = to_values(env, info[0]);
        if (!info[1].IsNumber() || !info[2].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions and precision must be numbers");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
        const int precision = info[2].As<Napi::Number>().Int32Value();

        return encode_geometry_frame_with_workspace(
            env,
            WKP_GEOMETRY_POINT,
            coords,
            dimensions,
            precision,
            std::vector<size_t>{1},
            std::vector<size_t>{1});
    }

    Napi::Value EncodeGeometryFrameF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 6)
        {
            throw Napi::TypeError::New(env, "encodeGeometryFrameF64(geometryType, coords, dimensions, precision, groupSegmentCounts, segmentPointCounts) expects 6 arguments");
        }

        if (!info[0].IsNumber())
        {
            throw Napi::TypeError::New(env, "geometryType must be a number");
        }

        const int geometry_type = info[0].As<Napi::Number>().Int32Value();
        std::vector<double> coords = to_values(env, info[1]);

        if (!info[2].IsNumber() || !info[3].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions and precision must be numbers");
        }

        const size_t dimensions = static_cast<size_t>(info[2].As<Napi::Number>().Uint32Value());
        const int precision = info[3].As<Napi::Number>().Int32Value();
        std::vector<size_t> group_segment_counts = to_counts(env, info[4], "groupSegmentCounts");
        std::vector<size_t> segment_point_counts = to_counts(env, info[5], "segmentPointCounts");

        return encode_geometry_frame_with_workspace(
            env,
            geometry_type,
            coords,
            dimensions,
            precision,
            group_segment_counts,
            segment_point_counts);
    }

    Napi::Value EncodeLineStringF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 3)
        {
            throw Napi::TypeError::New(env, "encodeLineStringF64(coords, dimensions, precision) expects 3 arguments");
        }

        std::vector<double> coords = to_values(env, info[0]);
        if (!info[1].IsNumber() || !info[2].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions and precision must be numbers");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
        const int precision = info[2].As<Napi::Number>().Int32Value();

        return encode_geometry_frame_with_workspace(
            env,
            WKP_GEOMETRY_LINESTRING,
            coords,
            dimensions,
            precision,
            std::vector<size_t>{1},
            std::vector<size_t>{coords.size() / dimensions});
    }

    Napi::Value EncodePolygonF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 4)
        {
            throw Napi::TypeError::New(env, "encodePolygonF64(coords, dimensions, precision, ringPointCounts) expects 4 arguments");
        }

        std::vector<double> coords = to_values(env, info[0]);
        if (!info[1].IsNumber() || !info[2].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions and precision must be numbers");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
        const int precision = info[2].As<Napi::Number>().Int32Value();
        std::vector<size_t> ring_counts = to_counts(env, info[3], "ringPointCounts");

        return encode_geometry_frame_with_workspace(
            env,
            WKP_GEOMETRY_POLYGON,
            coords,
            dimensions,
            precision,
            std::vector<size_t>{ring_counts.size()},
            ring_counts);
    }

    Napi::Value EncodeMultiPointF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 4)
        {
            throw Napi::TypeError::New(env, "encodeMultiPointF64(coords, dimensions, precision, pointCount) expects 4 arguments");
        }

        std::vector<double> coords = to_values(env, info[0]);
        if (!info[1].IsNumber() || !info[2].IsNumber() || !info[3].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions, precision, and pointCount must be numbers");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
        const int precision = info[2].As<Napi::Number>().Int32Value();
        const size_t point_count = static_cast<size_t>(info[3].As<Napi::Number>().Uint32Value());

        std::vector<size_t> group_segment_counts(point_count, 1);
        std::vector<size_t> segment_point_counts(point_count, 1);
        return encode_geometry_frame_with_workspace(
            env,
            WKP_GEOMETRY_MULTIPOINT,
            coords,
            dimensions,
            precision,
            group_segment_counts,
            segment_point_counts);
    }

    Napi::Value EncodeMultiLineStringF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 4)
        {
            throw Napi::TypeError::New(env, "encodeMultiLineStringF64(coords, dimensions, precision, linePointCounts) expects 4 arguments");
        }

        std::vector<double> coords = to_values(env, info[0]);
        if (!info[1].IsNumber() || !info[2].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions and precision must be numbers");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
        const int precision = info[2].As<Napi::Number>().Int32Value();
        std::vector<size_t> line_counts = to_counts(env, info[3], "linePointCounts");

        std::vector<size_t> group_segment_counts(line_counts.size(), 1);
        return encode_geometry_frame_with_workspace(
            env,
            WKP_GEOMETRY_MULTILINESTRING,
            coords,
            dimensions,
            precision,
            group_segment_counts,
            line_counts);
    }

    Napi::Value EncodeMultiPolygonF64(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 5)
        {
            throw Napi::TypeError::New(env, "encodeMultiPolygonF64(coords, dimensions, precision, polygonRingCounts, ringPointCounts) expects 5 arguments");
        }

        std::vector<double> coords = to_values(env, info[0]);
        if (!info[1].IsNumber() || !info[2].IsNumber())
        {
            throw Napi::TypeError::New(env, "dimensions and precision must be numbers");
        }
        const size_t dimensions = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
        const int precision = info[2].As<Napi::Number>().Int32Value();
        std::vector<size_t> polygon_ring_counts = to_counts(env, info[3], "polygonRingCounts");
        std::vector<size_t> ring_point_counts = to_counts(env, info[4], "ringPointCounts");

        return encode_geometry_frame_with_workspace(
            env,
            WKP_GEOMETRY_MULTIPOLYGON,
            coords,
            dimensions,
            precision,
            polygon_ring_counts,
            ring_point_counts);
    }

    Napi::Array rows_from_coords(const Napi::Env &env, const double *coords, size_t point_count, int dimensions)
    {
        if (dimensions <= 0)
        {
            throw Napi::Error::New(env, "Decoded geometry segment has invalid coordinate length");
        }

        Napi::Array rows = Napi::Array::New(env, point_count);
        for (size_t r = 0; r < point_count; ++r)
        {
            Napi::Array row = Napi::Array::New(env, static_cast<uint32_t>(dimensions));
            for (int d = 0; d < dimensions; ++d)
            {
                row.Set(static_cast<uint32_t>(d), Napi::Number::New(env, coords[(r * static_cast<size_t>(dimensions)) + static_cast<size_t>(d)]));
            }
            rows.Set(static_cast<uint32_t>(r), row);
        }
        return rows;
    }

    Napi::Value DecodeGeometryFrame(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (info.Length() < 1)
        {
            throw Napi::TypeError::New(env, "decodeGeometryFrame(encoded) expects 1 argument");
        }

        std::vector<uint8_t> encoded = to_encoded_bytes(env, info[0]);
        wkp_context ctx{};
        wkp_status status = wkp_context_init(&ctx);
        throw_for_status(env, status, nullptr);

        struct ContextHolder
        {
            wkp_context *ptr;
            ~ContextHolder()
            {
                wkp_context_free(ptr);
            }
        } context_holder{&ctx};

        const wkp_geometry_frame_f64 *frame = nullptr;
        status = wkp_decode_geometry_frame(
            &ctx,
            encoded.data(),
            encoded.size(),
            &frame);
        if (status != WKP_STATUS_OK)
        {
            throw_for_status(env, status, nullptr);
        }

        Napi::Object geometry = Napi::Object::New(env);
        const int gtype = frame->geometry_type;
        const int dims = frame->dimensions;
        size_t segment_index = 0;
        size_t coord_index = 0;

        auto next_segment_rows = [&]() -> Napi::Array
        {
            const size_t point_count = frame->segment_point_counts[segment_index++];
            const double *segment_coords = frame->coords + coord_index;
            coord_index += point_count * static_cast<size_t>(dims);
            return rows_from_coords(env, segment_coords, point_count, dims);
        };

        if (gtype == WKP_GEOMETRY_POINT)
        {
            geometry.Set("type", Napi::String::New(env, "Point"));
            const Napi::Array rows = next_segment_rows();
            geometry.Set("coordinates", rows.Get(static_cast<uint32_t>(0)));
        }
        else if (gtype == WKP_GEOMETRY_LINESTRING)
        {
            geometry.Set("type", Napi::String::New(env, "LineString"));
            geometry.Set("coordinates", next_segment_rows());
        }
        else if (gtype == WKP_GEOMETRY_POLYGON)
        {
            geometry.Set("type", Napi::String::New(env, "Polygon"));
            const size_t ring_count = frame->group_segment_counts[0];
            Napi::Array rings = Napi::Array::New(env, ring_count);
            for (size_t i = 0; i < ring_count; ++i)
            {
                rings.Set(static_cast<uint32_t>(i), next_segment_rows());
            }
            geometry.Set("coordinates", rings);
        }
        else if (gtype == WKP_GEOMETRY_MULTIPOINT)
        {
            geometry.Set("type", Napi::String::New(env, "MultiPoint"));
            Napi::Array points = Napi::Array::New(env, frame->group_count);
            for (size_t i = 0; i < frame->group_count; ++i)
            {
                const Napi::Array rows = next_segment_rows();
                points.Set(static_cast<uint32_t>(i), rows.Get(static_cast<uint32_t>(0)));
            }
            geometry.Set("coordinates", points);
        }
        else if (gtype == WKP_GEOMETRY_MULTILINESTRING)
        {
            geometry.Set("type", Napi::String::New(env, "MultiLineString"));
            Napi::Array lines = Napi::Array::New(env, frame->group_count);
            for (size_t i = 0; i < frame->group_count; ++i)
            {
                lines.Set(static_cast<uint32_t>(i), next_segment_rows());
            }
            geometry.Set("coordinates", lines);
        }
        else if (gtype == WKP_GEOMETRY_MULTIPOLYGON)
        {
            geometry.Set("type", Napi::String::New(env, "MultiPolygon"));
            Napi::Array polygons = Napi::Array::New(env, frame->group_count);
            for (size_t p = 0; p < frame->group_count; ++p)
            {
                const size_t ring_count = frame->group_segment_counts[p];
                Napi::Array rings = Napi::Array::New(env, ring_count);
                for (size_t r = 0; r < ring_count; ++r)
                {
                    rings.Set(static_cast<uint32_t>(r), next_segment_rows());
                }
                polygons.Set(static_cast<uint32_t>(p), rings);
            }
            geometry.Set("coordinates", polygons);
        }
        else
        {
            throw Napi::TypeError::New(env, "Unsupported geometry type in header");
        }

        Napi::Object out = Napi::Object::New(env);
        out.Set("version", Napi::Number::New(env, frame->version));
        out.Set("precision", Napi::Number::New(env, frame->precision));
        out.Set("dimensions", Napi::Number::New(env, frame->dimensions));
        out.Set("geometry", geometry);

        return out;
    }

    Napi::Value RunSelfTest(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        int failed_check = 0;
        const wkp_status status = wkp_basic_self_test(&failed_check);
        if (status != WKP_STATUS_OK)
        {
            throw Napi::Error::New(env, "WKP core self-test failed (check " + std::to_string(failed_check) + ")");
        }
        return Napi::Boolean::New(env, true);
    }

} // namespace

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("encodeF64", Napi::Function::New(env, EncodeF64));
    exports.Set("decodeF64", Napi::Function::New(env, DecodeF64));
    exports.Set("decodeGeometryHeader", Napi::Function::New(env, DecodeGeometryHeader));
    exports.Set("decodeGeometryFrame", Napi::Function::New(env, DecodeGeometryFrame));
    exports.Set("encodeGeometryFrameF64", Napi::Function::New(env, EncodeGeometryFrameF64));
    exports.Set("encodePointF64", Napi::Function::New(env, EncodePointF64));
    exports.Set("encodeLineStringF64", Napi::Function::New(env, EncodeLineStringF64));
    exports.Set("encodePolygonF64", Napi::Function::New(env, EncodePolygonF64));
    exports.Set("encodeMultiPointF64", Napi::Function::New(env, EncodeMultiPointF64));
    exports.Set("encodeMultiLineStringF64", Napi::Function::New(env, EncodeMultiLineStringF64));
    exports.Set("encodeMultiPolygonF64", Napi::Function::New(env, EncodeMultiPolygonF64));
    exports.Set("runSelfTest", Napi::Function::New(env, RunSelfTest));
    exports.Set("coreVersion", Napi::Function::New(env, [](const Napi::CallbackInfo &info)
                                                   { return Napi::String::New(info.Env(), WKP_CORE_VERSION); }));

    Napi::Object geometryType = Napi::Object::New(env);
    geometryType.Set("POINT", Napi::Number::New(env, WKP_GEOMETRY_POINT));
    geometryType.Set("LINESTRING", Napi::Number::New(env, WKP_GEOMETRY_LINESTRING));
    geometryType.Set("POLYGON", Napi::Number::New(env, WKP_GEOMETRY_POLYGON));
    geometryType.Set("MULTIPOINT", Napi::Number::New(env, WKP_GEOMETRY_MULTIPOINT));
    geometryType.Set("MULTILINESTRING", Napi::Number::New(env, WKP_GEOMETRY_MULTILINESTRING));
    geometryType.Set("MULTIPOLYGON", Napi::Number::New(env, WKP_GEOMETRY_MULTIPOLYGON));
    exports.Set("EncodedGeometryType", geometryType);

    return exports;
}

NODE_API_MODULE(wkp_node, Init)
