#include <napi.h>

#include <cstring>
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

        char error_message[512] = {0};
        std::vector<uint8_t> scratch(4096);

        while (true)
        {
            wkp_u8_buffer out{scratch.data(), scratch.size()};
            const wkp_status status = wkp_encode_f64_into(
                values.data(),
                values.size(),
                dimensions,
                precisions.data(),
                precisions.size(),
                &out,
                error_message,
                sizeof(error_message));

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                scratch.resize(out.size);
                continue;
            }

            throw_for_status(env, status, error_message);
            return Napi::Buffer<uint8_t>::Copy(env, scratch.data(), out.size);
        }
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

        char error_message[512] = {0};
        std::vector<double> scratch(std::max<size_t>(dimensions, 64));

        while (true)
        {
            wkp_f64_buffer out{scratch.data(), scratch.size()};
            const wkp_status status = wkp_decode_f64_into(
                encoded.data(),
                encoded.size(),
                dimensions,
                precisions.data(),
                precisions.size(),
                &out,
                error_message,
                sizeof(error_message));

            if (status == WKP_STATUS_BUFFER_TOO_SMALL)
            {
                scratch.resize(out.size);
                continue;
            }

            throw_for_status(env, status, error_message);
            Napi::ArrayBuffer buffer = Napi::ArrayBuffer::New(env, out.size * sizeof(double));
            std::memcpy(buffer.Data(), scratch.data(), out.size * sizeof(double));
            return Napi::Float64Array::New(env, out.size, buffer, 0, napi_float64_array);
        }
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
        char error_message[512] = {0};

        const wkp_status status = wkp_decode_geometry_header(
            encoded.data(),
            encoded.size(),
            &version,
            &precision,
            &dimensions,
            &geometry_type,
            error_message,
            sizeof(error_message));

        throw_for_status(env, status, error_message);

        Napi::Array out = Napi::Array::New(env, 4);
        out.Set(static_cast<uint32_t>(0), Napi::Number::New(env, version));
        out.Set(static_cast<uint32_t>(1), Napi::Number::New(env, precision));
        out.Set(static_cast<uint32_t>(2), Napi::Number::New(env, dimensions));
        out.Set(static_cast<uint32_t>(3), Napi::Number::New(env, geometry_type));
        return out;
    }

} // namespace

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("encodeF64", Napi::Function::New(env, EncodeF64));
    exports.Set("decodeF64", Napi::Function::New(env, DecodeF64));
    exports.Set("decodeGeometryHeader", Napi::Function::New(env, DecodeGeometryHeader));
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
