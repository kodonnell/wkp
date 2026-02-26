#include <napi.h>

#include <cstring>
#include <string>
#include <vector>

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

        wkp_u8_buffer out{};
        char error_message[512] = {0};

        const wkp_status status = wkp_encode_f64(
            values.data(),
            values.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &out,
            error_message,
            sizeof(error_message));

        throw_for_status(env, status, error_message);

        Napi::Buffer<uint8_t> result = Napi::Buffer<uint8_t>::Copy(env, out.data, out.size);
        wkp_free_u8_buffer(&out);
        return result;
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

        wkp_f64_buffer out{};
        char error_message[512] = {0};

        const wkp_status status = wkp_decode_f64(
            encoded.data(),
            encoded.size(),
            dimensions,
            precisions.data(),
            precisions.size(),
            &out,
            error_message,
            sizeof(error_message));

        throw_for_status(env, status, error_message);

        Napi::ArrayBuffer buffer = Napi::ArrayBuffer::New(env, out.size * sizeof(double));
        std::memcpy(buffer.Data(), out.data, out.size * sizeof(double));
        wkp_free_f64_buffer(&out);

        return Napi::Float64Array::New(env, out.size, buffer, 0, napi_float64_array);
    }

} // namespace

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("encodeF64", Napi::Function::New(env, EncodeF64));
    exports.Set("decodeF64", Napi::Function::New(env, DecodeF64));
    return exports;
}

NODE_API_MODULE(wkp_node, Init)
