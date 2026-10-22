using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Wkp;

/// <summary>Geometry type codes stored in the WKP header.</summary>
public enum GeometryType
{
    Point = 1,
    LineString = 2,
    Polygon = 3,
    MultiPoint = 4,
    MultiLineString = 5,
    MultiPolygon = 6,
}

// Not a positional record, so that a field added later does not change a Deconstruct signature.
/// <summary>The four header fields of an encoded geometry.</summary>
public readonly record struct Header
{
    /// <summary>Creates a header.</summary>
    public Header(int version, int precision, int dimensions, GeometryType geometryType)
    {
        Version = version;
        Precision = precision;
        Dimensions = dimensions;
        GeometryType = geometryType;
    }

    /// <summary>Format version.</summary>
    public int Version { get; init; }

    /// <summary>Decimal places kept.</summary>
    public int Precision { get; init; }

    /// <summary>Values per point.</summary>
    public int Dimensions { get; init; }

    /// <summary>The kind of geometry.</summary>
    public GeometryType GeometryType { get; init; }
}

/// <summary>
/// Flat form of a geometry: interleaved coordinates plus topology. Groups are polygons/parts and
/// segments are rings/linestrings/points:
/// <list type="bullet">
/// <item>Point, LineString: 1 group with 1 segment.</item>
/// <item>Polygon: 1 group with one segment per ring (shell first).</item>
/// <item>MultiPoint, MultiLineString, MultiPolygon: one group per part.</item>
/// </list>
/// </summary>
public sealed class GeometryFrame
{
    /// <summary>Size of the fixed header written by <see cref="ToBuffer"/>.</summary>
    public const int BufferHeaderSize = 40;

    /// <summary>
    /// Creates a frame. <paramref name="precision"/> is the number of decimal places kept when it
    /// is encoded; <paramref name="version"/> is the format version, and only 1 can be encoded.
    /// </summary>
    public GeometryFrame(
        GeometryType geometryType,
        int dimensions,
        double[] coords,
        int[] segmentPointCounts,
        int[] groupSegmentCounts,
        int precision,
        int version = 1)
    {
        GeometryType = geometryType;
        Dimensions = dimensions;
        Coords = coords ?? throw new ArgumentNullException(nameof(coords));
        SegmentPointCounts = segmentPointCounts ?? throw new ArgumentNullException(nameof(segmentPointCounts));
        GroupSegmentCounts = groupSegmentCounts ?? throw new ArgumentNullException(nameof(groupSegmentCounts));
        Precision = precision;
        Version = version;
    }

    /// <summary>Format version. Decoding sets it; encoding requires 1.</summary>
    public int Version { get; }
    /// <summary>Decimal places kept. Used by <see cref="WkpContext.EncodeFrame"/>; set by decoding.</summary>
    public int Precision { get; }
    public int Dimensions { get; }
    public GeometryType GeometryType { get; }
    /// <summary>Point-major, dimension-interleaved coordinates: <c>[x0, y0, x1, y1, ...]</c>.</summary>
    public double[] Coords { get; }
    public int[] SegmentPointCounts { get; }
    public int[] GroupSegmentCounts { get; }

    public int PointCount => Dimensions == 0 ? 0 : Coords.Length / Dimensions;

    /// <summary>
    /// Serializes to the cross-language binary frame format (little-endian; same as Python's
    /// <c>GeometryFrame.to_buffer</c> and JavaScript's <c>toBuffer</c>): int32 version, precision,
    /// dimensions, geometry type; uint64 coord, segment and group counts; then float64 coords,
    /// uint32 segment point counts and uint32 group segment counts.
    /// </summary>
    public byte[] ToBuffer()
    {
        var buffer = new byte[BufferHeaderSize + Coords.Length * 8 + (SegmentPointCounts.Length + GroupSegmentCounts.Length) * 4];
        var span = buffer.AsSpan();
        BinaryPrimitives.WriteInt32LittleEndian(span[0..], Version);
        BinaryPrimitives.WriteInt32LittleEndian(span[4..], Precision);
        BinaryPrimitives.WriteInt32LittleEndian(span[8..], Dimensions);
        BinaryPrimitives.WriteInt32LittleEndian(span[12..], (int)GeometryType);
        BinaryPrimitives.WriteUInt64LittleEndian(span[16..], (ulong)Coords.Length);
        BinaryPrimitives.WriteUInt64LittleEndian(span[24..], (ulong)SegmentPointCounts.Length);
        BinaryPrimitives.WriteUInt64LittleEndian(span[32..], (ulong)GroupSegmentCounts.Length);
        var o = BufferHeaderSize;
        foreach (var c in Coords)
        {
            BinaryPrimitives.WriteDoubleLittleEndian(span[o..], c);
            o += 8;
        }
        foreach (var n in SegmentPointCounts)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(span[o..], checked((uint)n));
            o += 4;
        }
        foreach (var n in GroupSegmentCounts)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(span[o..], checked((uint)n));
            o += 4;
        }
        return buffer;
    }

    /// <summary>Deserializes a buffer produced by <see cref="ToBuffer"/> (in any binding).</summary>
    public static GeometryFrame FromBuffer(ReadOnlySpan<byte> buffer)
    {
        if (buffer.Length < BufferHeaderSize)
        {
            throw new FormatException("GeometryFrame buffer is shorter than its header");
        }
        var version = BinaryPrimitives.ReadInt32LittleEndian(buffer[0..]);
        var precision = BinaryPrimitives.ReadInt32LittleEndian(buffer[4..]);
        var dimensions = BinaryPrimitives.ReadInt32LittleEndian(buffer[8..]);
        var geometryType = (GeometryType)BinaryPrimitives.ReadInt32LittleEndian(buffer[12..]);
        var coordCount = checked((int)BinaryPrimitives.ReadUInt64LittleEndian(buffer[16..]));
        var segmentCount = checked((int)BinaryPrimitives.ReadUInt64LittleEndian(buffer[24..]));
        var groupCount = checked((int)BinaryPrimitives.ReadUInt64LittleEndian(buffer[32..]));
        if (buffer.Length != BufferHeaderSize + (long)coordCount * 8 + ((long)segmentCount + groupCount) * 4)
        {
            throw new FormatException("GeometryFrame buffer length does not match its header");
        }
        var o = BufferHeaderSize;
        var coords = new double[coordCount];
        for (var i = 0; i < coordCount; i++, o += 8)
        {
            coords[i] = BinaryPrimitives.ReadDoubleLittleEndian(buffer[o..]);
        }
        var segments = new int[segmentCount];
        for (var i = 0; i < segmentCount; i++, o += 4)
        {
            segments[i] = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(buffer[o..]));
        }
        var groups = new int[groupCount];
        for (var i = 0; i < groupCount; i++, o += 4)
        {
            groups[i] = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(buffer[o..]));
        }
        return new GeometryFrame(geometryType, dimensions, coords, segments, groups, precision, version);
    }
}

/// <summary>
/// Reusable native buffers for encoding and decoding. Not thread-safe: use one per thread, or the
/// static <see cref="WkpCodec"/> methods, which keep one per thread for you.
/// </summary>
public sealed unsafe class WkpContext : IDisposable
{
    // Results point into the native context's buffers until the next call, so each method copies
    // them out before returning, and keeps this object alive until then (GC.KeepAlive).
    private readonly Native.ContextHandle _ctx = Native.ContextNew();

    /// <summary>Creates a context with empty buffers.</summary>
    public WkpContext()
    {
    }

    /// <summary>Version of the native library, e.g. <c>0.7.0</c>.</summary>
    public static string NativeVersion => Marshal.PtrToStringUTF8((IntPtr)Native.Version()) ?? "";

    /// <summary>Frees the native buffers. Later calls throw <see cref="ObjectDisposedException"/>.</summary>
    public void Dispose() => _ctx.Dispose();

    private Native.ContextHandle Ctx => _ctx.IsClosed ? throw new ObjectDisposedException(nameof(WkpContext)) : _ctx;

    /// <summary>Encodes a geometry frame at <see cref="GeometryFrame.Precision"/> to ASCII bytes.</summary>
    public byte[] EncodeFrameToBytes(GeometryFrame frame)
    {
        ArgumentNullException.ThrowIfNull(frame);
        if (frame.Dimensions <= 0)
        {
            throw new ArgumentException("dimensions must be positive", nameof(frame));
        }
        if (frame.Version != 1)
        {
            throw new ArgumentException($"only format version 1 can be encoded, not {frame.Version}", nameof(frame));
        }
        var groups = ToNative(frame.GroupSegmentCounts, nameof(frame.GroupSegmentCounts));
        var segments = ToNative(frame.SegmentPointCounts, nameof(frame.SegmentPointCounts));
        byte* data;
        nuint size;
        fixed (double* coords = frame.Coords)
        fixed (nuint* g = groups)
        fixed (nuint* s = segments)
        {
            Native.Check(Native.EncodeGeometryFrame(
                Ctx, (int)frame.GeometryType, coords, (nuint)frame.Coords.Length, (nuint)frame.Dimensions, frame.Precision,
                s, (nuint)segments.Length, g, (nuint)groups.Length, &data, &size));
        }
        var result = new ReadOnlySpan<byte>(data, checked((int)size)).ToArray();
        GC.KeepAlive(this);
        return result;
    }

    /// <summary>Encodes a geometry frame at <see cref="GeometryFrame.Precision"/>.</summary>
    public string EncodeFrame(GeometryFrame frame) => Encoding.ASCII.GetString(EncodeFrameToBytes(frame));

    /// <summary>Encodes a geometry from its flat parts. See <see cref="GeometryFrame"/> for the topology.</summary>
    public string Encode(GeometryType geometryType, ReadOnlySpan<double> coords, int dimensions, int precision,
        ReadOnlySpan<int> segmentPointCounts, ReadOnlySpan<int> groupSegmentCounts)
    {
        return EncodeFrame(new GeometryFrame(geometryType, dimensions, coords.ToArray(), segmentPointCounts.ToArray(),
            groupSegmentCounts.ToArray(), precision));
    }

    /// <summary>Decodes an encoded geometry to a frame.</summary>
    public GeometryFrame Decode(ReadOnlySpan<byte> encoded)
    {
        Native.Frame* f;
        fixed (byte* e = encoded)
        {
            Native.Check(Native.DecodeGeometryFrame(Ctx, e, (nuint)encoded.Length, &f));
        }
        var coords = new ReadOnlySpan<double>(f->Coords, checked((int)f->CoordValueCount)).ToArray();
        var segments = FromNative(f->SegmentPointCounts, f->SegmentCount);
        var groups = FromNative(f->GroupSegmentCounts, f->GroupCount);
        var frame = new GeometryFrame((GeometryType)f->GeometryType, f->Dimensions, coords, segments, groups, f->Precision, f->Version);
        GC.KeepAlive(this);
        return frame;
    }

    /// <inheritdoc cref="Decode(ReadOnlySpan{byte})"/>
    public GeometryFrame Decode(string encoded) => Decode(Ascii(encoded));

    /// <summary>
    /// Encodes a headerless stream of interleaved values. The number of dimensions is
    /// <c>precisions.Length</c> (one precision per dimension).
    /// </summary>
    public string EncodeFloats(ReadOnlySpan<double> values, ReadOnlySpan<int> precisions)
    {
        byte* data;
        nuint size;
        fixed (double* v = values)
        fixed (int* p = precisions)
        {
            Native.Check(Native.EncodeF64(Ctx, v, (nuint)values.Length, (nuint)precisions.Length, p, (nuint)precisions.Length, &data, &size));
        }
        var result = Encoding.ASCII.GetString(data, checked((int)size));
        GC.KeepAlive(this);
        return result;
    }

    /// <summary>Decodes a stream from <see cref="EncodeFloats"/> to interleaved values.</summary>
    public double[] DecodeFloats(ReadOnlySpan<byte> encoded, ReadOnlySpan<int> precisions)
    {
        double* values;
        nuint size;
        fixed (byte* e = encoded)
        fixed (int* p = precisions)
        {
            Native.Check(Native.DecodeF64(Ctx, e, (nuint)encoded.Length, (nuint)precisions.Length, p, (nuint)precisions.Length, &values, &size));
        }
        var result = new ReadOnlySpan<double>(values, checked((int)size)).ToArray();
        GC.KeepAlive(this);
        return result;
    }

    /// <inheritdoc cref="DecodeFloats(ReadOnlySpan{byte}, ReadOnlySpan{int})"/>
    public double[] DecodeFloats(string encoded, ReadOnlySpan<int> precisions) => DecodeFloats(Ascii(encoded), precisions);

    /// <summary>Reads only the header of an encoded geometry.</summary>
    public static Header DecodeHeader(ReadOnlySpan<byte> encoded)
    {
        int version, precision, dimensions, geometryType;
        fixed (byte* e = encoded)
        {
            Native.Check(Native.DecodeGeometryHeader(e, (nuint)encoded.Length, &version, &precision, &dimensions, &geometryType));
        }
        return new Header(version, precision, dimensions, (GeometryType)geometryType);
    }

    /// <inheritdoc cref="DecodeHeader(ReadOnlySpan{byte})"/>
    public static Header DecodeHeader(string encoded) => DecodeHeader(Ascii(encoded));

    /// <summary>
    /// The string as bytes. Encoded WKP is ASCII; any other char is rejected rather than replaced
    /// (Encoding.ASCII would turn it into <c>?</c>, which is a valid WKP char).
    /// </summary>
    private static byte[] Ascii(string s)
    {
        ArgumentNullException.ThrowIfNull(s);
        var bytes = new byte[s.Length];
        for (var i = 0; i < s.Length; i++)
        {
            bytes[i] = s[i] <= 0x7F ? (byte)s[i] : throw new FormatException($"WKP input contains a non-ASCII char at index {i}");
        }
        return bytes;
    }

    private static nuint[] ToNative(int[] counts, string name)
    {
        var result = new nuint[counts.Length];
        for (var i = 0; i < counts.Length; i++)
        {
            result[i] = counts[i] >= 0 ? (nuint)counts[i] : throw new ArgumentException("counts cannot be negative", name);
        }
        return result;
    }

    private static int[] FromNative(nuint* counts, nuint length)
    {
        var result = new int[checked((int)length)];
        for (var i = 0; i < result.Length; i++)
        {
            result[i] = checked((int)counts[i]);
        }
        return result;
    }
}

/// <summary>Static encode/decode using a per-thread <see cref="WkpContext"/>.</summary>
public static class WkpCodec
{
    private static readonly ThreadLocal<WkpContext> Context = new(() => new WkpContext());

    /// <inheritdoc cref="WkpContext.EncodeFrame"/>
    public static string EncodeFrame(GeometryFrame frame) => Context.Value!.EncodeFrame(frame);

    /// <inheritdoc cref="WkpContext.Encode"/>
    public static string Encode(GeometryType geometryType, ReadOnlySpan<double> coords, int dimensions, int precision,
        ReadOnlySpan<int> segmentPointCounts, ReadOnlySpan<int> groupSegmentCounts) =>
        Context.Value!.Encode(geometryType, coords, dimensions, precision, segmentPointCounts, groupSegmentCounts);

    /// <inheritdoc cref="WkpContext.Decode(ReadOnlySpan{byte})"/>
    public static GeometryFrame Decode(ReadOnlySpan<byte> encoded) => Context.Value!.Decode(encoded);

    /// <inheritdoc cref="WkpContext.Decode(ReadOnlySpan{byte})"/>
    public static GeometryFrame Decode(string encoded) => Context.Value!.Decode(encoded);

    /// <inheritdoc cref="WkpContext.DecodeHeader(ReadOnlySpan{byte})"/>
    public static Header DecodeHeader(string encoded) => WkpContext.DecodeHeader(encoded);

    /// <inheritdoc cref="WkpContext.EncodeFloats"/>
    public static string EncodeFloats(ReadOnlySpan<double> values, ReadOnlySpan<int> precisions) =>
        Context.Value!.EncodeFloats(values, precisions);

    /// <inheritdoc cref="WkpContext.DecodeFloats(ReadOnlySpan{byte}, ReadOnlySpan{int})"/>
    public static double[] DecodeFloats(string encoded, ReadOnlySpan<int> precisions) =>
        Context.Value!.DecodeFloats(encoded, precisions);
}
