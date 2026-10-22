using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Wkp;

/// <summary>P/Invoke declarations for the C ABI in bindings/c/include/wkp/core.h.</summary>
internal static unsafe class Native
{
    // The C ABI library (libwkp) renamed: on case-insensitive file systems "wkp.dll" would be this
    // managed assembly, Wkp.dll, which sits in the same directory.
    internal const string Library = "wkp_native";

    internal const int StatusOk = 0;
    internal const int StatusInvalidArgument = 1;
    internal const int StatusMalformedInput = 2;

    [StructLayout(LayoutKind.Sequential)]
    internal struct Frame
    {
        public int Version;
        public int Precision;
        public int Dimensions;
        public int GeometryType;
        public double* Coords;
        public nuint CoordValueCount;
        public nuint* SegmentPointCounts;
        public nuint SegmentCount;
        public nuint* GroupSegmentCounts;
        public nuint GroupCount;
    }

    /// <summary>Owns an opaque <c>wkp_context*</c> and frees it once nothing is using it.</summary>
    internal sealed class ContextHandle : SafeHandle
    {
        public ContextHandle() : base(IntPtr.Zero, ownsHandle: true)
        {
        }

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            ContextFree(handle);
            return true;
        }
    }

    [DllImport(Library, EntryPoint = "wkp_version")]
    internal static extern byte* Version();

    [DllImport(Library, EntryPoint = "wkp_context_new")]
    internal static extern ContextHandle ContextNew();

    [DllImport(Library, EntryPoint = "wkp_context_free")]
    private static extern void ContextFree(IntPtr ctx);

    [DllImport(Library, EntryPoint = "wkp_encode_f64")]
    internal static extern int EncodeF64(
        ContextHandle ctx, double* values, nuint valueCount, nuint dimensions, int* precisions, nuint precisionCount,
        byte** outData, nuint* outSize);

    [DllImport(Library, EntryPoint = "wkp_decode_f64")]
    internal static extern int DecodeF64(
        ContextHandle ctx, byte* encoded, nuint encodedSize, nuint dimensions, int* precisions, nuint precisionCount,
        double** outValues, nuint* outSize);

    [DllImport(Library, EntryPoint = "wkp_encode_geometry_frame")]
    internal static extern int EncodeGeometryFrame(
        ContextHandle ctx, int geometryType, double* coords, nuint coordValueCount, nuint dimensions, int precision,
        nuint* segmentPointCounts, nuint segmentCount, nuint* groupSegmentCounts, nuint groupCount,
        byte** outData, nuint* outSize);

    [DllImport(Library, EntryPoint = "wkp_decode_geometry_frame")]
    internal static extern int DecodeGeometryFrame(ContextHandle ctx, byte* encoded, nuint encodedSize, Frame** outFrame);

    [DllImport(Library, EntryPoint = "wkp_decode_geometry_header")]
    internal static extern int DecodeGeometryHeader(
        byte* encoded, nuint encodedSize, int* version, int* precision, int* dimensions, int* geometryType);

    internal static void Check(int status)
    {
        switch (status)
        {
            case StatusOk:
                return;
            case StatusInvalidArgument:
                throw new ArgumentException("WKP core error: invalid argument");
            case StatusMalformedInput:
                throw new FormatException("WKP core error: malformed input");
            default:
                throw new InvalidOperationException($"WKP core error (status {status})");
        }
    }

    /// <summary>
    /// Falls back to <c>runtimes/&lt;rid&gt;/native/</c> next to the assembly, which is where the
    /// library lands when this project is referenced directly rather than as a package.
    /// </summary>
#pragma warning disable CA2255 // Module initializers are intended for app code, but this one only registers a resolver.
    [ModuleInitializer]
#pragma warning restore CA2255
    internal static void RegisterResolver()
    {
        NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, Resolve);
    }

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != Library)
        {
            return IntPtr.Zero;
        }
        if (NativeLibrary.TryLoad(name, assembly, searchPath, out var handle))
        {
            return handle;
        }
        var file = OperatingSystem.IsWindows() ? "wkp_native.dll" : OperatingSystem.IsMacOS() ? "libwkp_native.dylib" : "libwkp_native.so";
        foreach (var rid in new[] { RuntimeInformation.RuntimeIdentifier, PortableRid() })
        {
            var path = Path.Combine(AppContext.BaseDirectory, "runtimes", rid, "native", file);
            if (NativeLibrary.TryLoad(path, out handle))
            {
                return handle;
            }
        }
        return IntPtr.Zero;
    }

    private static string PortableRid()
    {
        var os = OperatingSystem.IsWindows() ? "win" : OperatingSystem.IsMacOS() ? "osx" : "linux";
        var arch = RuntimeInformation.ProcessArchitecture switch
        {
            Architecture.X64 => "x64",
            Architecture.Arm64 => "arm64",
            Architecture.X86 => "x86",
            var other => other.ToString().ToLowerInvariant(),
        };
        return $"{os}-{arch}";
    }
}
