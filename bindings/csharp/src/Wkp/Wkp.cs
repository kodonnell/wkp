using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Wkp;

/// <summary>Geometry type codes stored in the WKP header.</summary>
public enum GeometryType
{
    /// <summary>Not a geometry type: the default value. Decoding never produces it.</summary>
    Unknown = 0,

    /// <summary>One point: one group with one segment of one point.</summary>
    Point = 1,

    /// <summary>One group with one segment of two or more points.</summary>
    LineString = 2,

    /// <summary>One group with one segment per ring (shell first).</summary>
    Polygon = 3,

    /// <summary>One group per point, each with one segment of one point.</summary>
    MultiPoint = 4,

    /// <summary>One group per linestring, each with one segment of two or more points.</summary>
    MultiLineString = 5,

    /// <summary>One group per polygon, each with one segment per ring.</summary>
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
/// segments are rings/linestrings/points (see <see cref="Wkp.GeometryType"/> for each type's
/// layout). Its arrays are read-only views; a frame built from arrays does not copy them, so do
/// not change those arrays while the frame is in use.
/// </summary>
public sealed class GeometryFrame
{
    /// <summary>Size of the fixed header written by <see cref="ToBuffer"/>.</summary>
    public static int BufferHeaderSize => 40;

    /// <summary>
    /// Creates a frame. <paramref name="precision"/> is the number of decimal places kept when it
    /// is encoded; <paramref name="version"/> is the format version, and only 1 can be encoded.
    /// Arrays convert implicitly to the <see cref="ReadOnlyMemory{T}"/> parameters.
    /// </summary>
    public GeometryFrame(
        GeometryType geometryType,
        int dimensions,
        ReadOnlyMemory<double> coords,
        ReadOnlyMemory<int> segmentPointCounts,
        ReadOnlyMemory<int> groupSegmentCounts,
        int precision,
        int version = 1)
    {
        GeometryType = geometryType;
        Dimensions = dimensions;
        Coords = coords;
        SegmentPointCounts = segmentPointCounts;
        GroupSegmentCounts = groupSegmentCounts;
        Precision = precision;
        Version = version;
    }

    /// <summary>Format version. Decoding sets it; encoding requires 1.</summary>
    public int Version { get; }

    /// <summary>Decimal places kept. Used by <see cref="WkpContext.EncodeFrame"/>; set by decoding.</summary>
    public int Precision { get; }

    /// <summary>Values per point.</summary>
    public int Dimensions { get; }

    /// <summary>The kind of geometry.</summary>
    public GeometryType GeometryType { get; }

    /// <summary>Point-major, dimension-interleaved coordinates: <c>[x0, y0, x1, y1, ...]</c>.</summary>
    public ReadOnlyMemory<double> Coords { get; }

    /// <summary>Points in each segment (ring, linestring or point).</summary>
    public ReadOnlyMemory<int> SegmentPointCounts { get; }

    /// <summary>Segments in each group (polygon or part).</summary>
    public ReadOnlyMemory<int> GroupSegmentCounts { get; }

    /// <summary>Number of points: <c>Coords.Length / Dimensions</c> (0 when dimensions is 0).</summary>
    public int PointCount => Dimensions == 0 ? 0 : Coords.Length / Dimensions;

    /// <summary>
    /// Serializes to the cross-language binary frame format (little-endian; same as Python's
    /// <c>GeometryFrame.to_buffer</c> and JavaScript's <c>toBuffer</c>): int32 version, precision,
    /// dimensions, geometry type; uint64 coord, segment and group counts; then float64 coords,
    /// uint32 segment point counts and uint32 group segment counts. Throws
    /// <see cref="OverflowException"/> for a negative count.
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
        foreach (var c in Coords.Span)
        {
            BinaryPrimitives.WriteDoubleLittleEndian(span[o..], c);
            o += 8;
        }
        foreach (var n in SegmentPointCounts.Span)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(span[o..], checked((uint)n));
            o += 4;
        }
        foreach (var n in GroupSegmentCounts.Span)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(span[o..], checked((uint)n));
            o += 4;
        }
        return buffer;
    }

    /// <summary>
    /// Deserializes a buffer produced by <see cref="ToBuffer"/> (in any binding). Throws
    /// <see cref="FormatException"/> if its length does not match its header, and
    /// <see cref="OverflowException"/> for counts above <see cref="int.MaxValue"/>.
    /// </summary>
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
/// static <see cref="WkpCodec"/> methods, which keep one per thread for you. Invalid arguments
/// throw <see cref="ArgumentException"/>; malformed encoded input (including a non-ASCII char)
/// throws <see cref="FormatException"/>.
/// </summary>
public sealed unsafe class WkpContext : IDisposable
{
    // Count arrays up to this length are converted on the stack.
    private const int StackCounts = 64;

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
        var result = EncodeFrameNative(frame).ToArray();
        GC.KeepAlive(this);
        return result;
    }

    /// <summary>Encodes a geometry frame at <see cref="GeometryFrame.Precision"/>.</summary>
    public string EncodeFrame(GeometryFrame frame)
    {
        var result = Encoding.ASCII.GetString(EncodeFrameNative(frame));
        GC.KeepAlive(this);
        return result;
    }

    /// <summary>Encodes a geometry from its flat parts. See <see cref="GeometryFrame"/> for the topology.</summary>
    public string Encode(GeometryType geometryType, ReadOnlySpan<double> coords, int dimensions, int precision,
        ReadOnlySpan<int> segmentPointCounts, ReadOnlySpan<int> groupSegmentCounts)
    {
        var result = Encoding.ASCII.GetString(
            EncodeNative(geometryType, coords, dimensions, precision, segmentPointCounts, groupSegmentCounts));
        GC.KeepAlive(this);
        return result;
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

    // The encoded bytes, in the native buffer: valid until the next call on this context.
    private ReadOnlySpan<byte> EncodeFrameNative(GeometryFrame frame)
    {
        ArgumentNullException.ThrowIfNull(frame);
        if (frame.Version != 1)
        {
            throw new ArgumentException($"only format version 1 can be encoded, not {frame.Version}", nameof(frame));
        }
        return EncodeNative(frame.GeometryType, frame.Coords.Span, frame.Dimensions, frame.Precision,
            frame.SegmentPointCounts.Span, frame.GroupSegmentCounts.Span);
    }

    // The encoded bytes, in the native buffer: valid until the next call on this context.
    private ReadOnlySpan<byte> EncodeNative(GeometryType geometryType, ReadOnlySpan<double> coords, int dimensions,
        int precision, ReadOnlySpan<int> segmentPointCounts, ReadOnlySpan<int> groupSegmentCounts)
    {
        if (dimensions <= 0)
        {
            throw new ArgumentException("dimensions must be positive", nameof(dimensions));
        }
        Span<nuint> segments = segmentPointCounts.Length <= StackCounts
            ? stackalloc nuint[segmentPointCounts.Length]
            : new nuint[segmentPointCounts.Length];
        Span<nuint> groups = groupSegmentCounts.Length <= StackCounts
            ? stackalloc nuint[groupSegmentCounts.Length]
            : new nuint[groupSegmentCounts.Length];
        ToNative(segmentPointCounts, segments, nameof(segmentPointCounts));
        ToNative(groupSegmentCounts, groups, nameof(groupSegmentCounts));
        byte* data;
        nuint size;
        fixed (double* c = coords)
        fixed (nuint* s = segments)
        fixed (nuint* g = groups)
        {
            Native.Check(Native.EncodeGeometryFrame(
                Ctx, (int)geometryType, c, (nuint)coords.Length, (nuint)dimensions, precision,
                s, (nuint)segments.Length, g, (nuint)groups.Length, &data, &size));
        }
        return new ReadOnlySpan<byte>(data, checked((int)size));
    }

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

    private static void ToNative(ReadOnlySpan<int> counts, Span<nuint> result, string name)
    {
        for (var i = 0; i < counts.Length; i++)
        {
            result[i] = counts[i] >= 0 ? (nuint)counts[i] : throw new ArgumentException("counts cannot be negative", name);
        }
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

/// <summary>
/// Static encode/decode using a per-thread <see cref="WkpContext"/>. Each thread's native buffers
/// grow to the largest input it has processed and live as long as the thread.
/// </summary>
public static class WkpCodec
{
    private static readonly ThreadLocal<WkpContext> Context = new(() => new WkpContext());

    /// <inheritdoc cref="WkpContext.NativeVersion"/>
    public static string NativeVersion => WkpContext.NativeVersion;

    /// <inheritdoc cref="WkpContext.EncodeFrame"/>
    public static string EncodeFrame(GeometryFrame frame) => Context.Value!.EncodeFrame(frame);

    /// <inheritdoc cref="WkpContext.EncodeFrameToBytes"/>
    public static byte[] EncodeFrameToBytes(GeometryFrame frame) => Context.Value!.EncodeFrameToBytes(frame);

    /// <inheritdoc cref="WkpContext.Encode"/>
    public static string Encode(GeometryType geometryType, ReadOnlySpan<double> coords, int dimensions, int precision,
        ReadOnlySpan<int> segmentPointCounts, ReadOnlySpan<int> groupSegmentCounts) =>
        Context.Value!.Encode(geometryType, coords, dimensions, precision, segmentPointCounts, groupSegmentCounts);

    /// <inheritdoc cref="WkpContext.Decode(ReadOnlySpan{byte})"/>
    public static GeometryFrame Decode(ReadOnlySpan<byte> encoded) => Context.Value!.Decode(encoded);

    /// <inheritdoc cref="WkpContext.Decode(ReadOnlySpan{byte})"/>
    public static GeometryFrame Decode(string encoded) => Context.Value!.Decode(encoded);

    /// <inheritdoc cref="WkpContext.DecodeHeader(ReadOnlySpan{byte})"/>
    public static Header DecodeHeader(ReadOnlySpan<byte> encoded) => WkpContext.DecodeHeader(encoded);

    /// <inheritdoc cref="WkpContext.DecodeHeader(ReadOnlySpan{byte})"/>
    public static Header DecodeHeader(string encoded) => WkpContext.DecodeHeader(encoded);

    /// <inheritdoc cref="WkpContext.EncodeFloats"/>
    public static string EncodeFloats(ReadOnlySpan<double> values, ReadOnlySpan<int> precisions) =>
        Context.Value!.EncodeFloats(values, precisions);

    /// <inheritdoc cref="WkpContext.DecodeFloats(ReadOnlySpan{byte}, ReadOnlySpan{int})"/>
    public static double[] DecodeFloats(ReadOnlySpan<byte> encoded, ReadOnlySpan<int> precisions) =>
        Context.Value!.DecodeFloats(encoded, precisions);

    /// <inheritdoc cref="WkpContext.DecodeFloats(ReadOnlySpan{byte}, ReadOnlySpan{int})"/>
    public static double[] DecodeFloats(string encoded, ReadOnlySpan<int> precisions) =>
        Context.Value!.DecodeFloats(encoded, precisions);
}
