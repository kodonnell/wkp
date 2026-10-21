using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Xunit;

namespace Wkp.Tests;

public class WkpTests
{
    private static readonly double[] Reference = { 38.5, -120.2, 40.7, -120.95, 43.252, -126.453 };

    [Fact]
    public void NativeVersionMatchesAssembly()
    {
        var assembly = typeof(WkpContext).Assembly.GetName().Version!;
        Assert.StartsWith($"{assembly.Major}.{assembly.Minor}.", WkpContext.NativeVersion + ".");
    }

    [Fact]
    public void GoogleReferenceFloats()
    {
        var encoded = WkpCodec.EncodeFloats(Reference, new[] { 5, 5 });
        Assert.Equal("_p~iF~ps|U_ulLnnqC_mqNvxq`@", encoded);
        Assert.Equal(Reference, WkpCodec.DecodeFloats(encoded, new[] { 5, 5 }));
    }

    [Fact]
    public void GoogleReferenceLineString()
    {
        var encoded = WkpCodec.Encode(GeometryType.LineString, Reference, 2, 5, new[] { 1 }, new[] { 3 });
        Assert.Equal("@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@", encoded);
        Assert.Equal(new Header(1, 5, 2, GeometryType.LineString), WkpCodec.DecodeHeader(encoded));
        var frame = WkpCodec.Decode(encoded);
        Assert.Equal(GeometryType.LineString, frame.GeometryType);
        Assert.Equal(5, frame.Precision);
        Assert.Equal(Reference, frame.Coords);
        Assert.Equal(new[] { 3 }, frame.SegmentPointCounts);
        Assert.Equal(new[] { 1 }, frame.GroupSegmentCounts);
    }

    public static IEnumerable<object[]> Geometries() => new[]
    {
        new object[] { new GeometryFrame(GeometryType.Point, 2, new[] { 1.0, 2.0 }, new[] { 1 }, new[] { 1 }, 6) },
        new object[] { new GeometryFrame(GeometryType.Polygon, 2, new[] { 0.0, 0, 4, 0, 4, 4, 0, 0, 1, 1, 2, 1, 2, 2, 1, 1 }, new[] { 4, 4 }, new[] { 2 }, 6) },
        new object[] { new GeometryFrame(GeometryType.MultiPoint, 3, new[] { 1.0, 2, 3, 4, 5, 6 }, new[] { 1, 1 }, new[] { 1, 1 }, 3) },
        new object[] { new GeometryFrame(GeometryType.MultiLineString, 2, new[] { 0.0, 0, 1, 1, 2, 2, 3, 3 }, new[] { 2, 2 }, new[] { 1, 1 }, 5) },
        new object[] { new GeometryFrame(GeometryType.MultiPolygon, 2, new[] { 0.0, 0, 1, 0, 1, 1, 0, 0, 5, 5, 6, 5, 6, 6, 5, 5 }, new[] { 4, 4 }, new[] { 1, 1 }, 6) },
    };

    [Theory]
    [MemberData(nameof(Geometries))]
    public void FrameRoundTrip(GeometryFrame frame)
    {
        using var ctx = new WkpContext();
        var decoded = ctx.Decode(ctx.EncodeFrame(frame));
        Assert.Equal(frame.GeometryType, decoded.GeometryType);
        Assert.Equal(frame.Dimensions, decoded.Dimensions);
        Assert.Equal(frame.Precision, decoded.Precision);
        Assert.Equal(frame.Coords, decoded.Coords);
        Assert.Equal(frame.SegmentPointCounts, decoded.SegmentPointCounts);
        Assert.Equal(frame.GroupSegmentCounts, decoded.GroupSegmentCounts);
        Assert.Equal(ctx.EncodeFrameToBytes(frame), ctx.EncodeFrameToBytes(decoded));
    }

    [Fact]
    public void BufferRoundTripAndHeaderSize()
    {
        var frame = WkpCodec.Decode("@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@");
        var buffer = frame.ToBuffer();
        Assert.Equal(GeometryFrame.BufferHeaderSize + 6 * 8 + 2 * 4, buffer.Length);
        var back = GeometryFrame.FromBuffer(buffer);
        Assert.Equal(frame.Coords, back.Coords);
        Assert.Equal((frame.Version, frame.Precision, frame.Dimensions, frame.GeometryType), (back.Version, back.Precision, back.Dimensions, back.GeometryType));
        Assert.Throws<FormatException>(() => GeometryFrame.FromBuffer(buffer.AsSpan(0, buffer.Length - 1)));
    }

    [Fact]
    public void ErrorsMapToExceptions()
    {
        Assert.Throws<ArgumentException>(() => WkpCodec.Encode(GeometryType.Point, new[] { 0.0, 0 }, 2, 9999, new[] { 1 }, new[] { 1 }));
        Assert.Throws<ArgumentException>(() => WkpCodec.EncodeFloats(new[] { double.NaN, 0 }, new[] { 5, 5 }));
        Assert.Throws<FormatException>(() => WkpCodec.Decode("@DAA_"));
        // Non-ASCII must not be silently replaced with '?', which is a valid WKP char.
        Assert.Throws<FormatException>(() => WkpCodec.Decode("@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`é"));
        Assert.Throws<ArgumentException>(() => WkpCodec.DecodeHeader("@@"));
        var ctx = new WkpContext();
        ctx.Dispose();
        Assert.Throws<ObjectDisposedException>(() => ctx.Decode("@DA@??"));
    }

    [Fact]
    public void EmptyFloatStreams()
    {
        Assert.Equal("", WkpCodec.EncodeFloats(Array.Empty<double>(), new[] { 5, 5 }));
        Assert.Empty(WkpCodec.DecodeFloats("", new[] { 5, 5 }));
    }

    [Fact]
    public void StaticCodecIsThreadSafe()
    {
        Parallel.For(0, 64, i =>
        {
            // k / 1000.0 is exactly what decoding at precision 3 produces.
            var values = Enumerable.Range(0, 1000).Select(j => (i * 1000 + j) / 1000.0).ToArray();
            var encoded = WkpCodec.EncodeFloats(values, new[] { 3 });
            Assert.Equal(values, WkpCodec.DecodeFloats(encoded, new[] { 3 }));
        });
    }

    // -- Shared fixtures in data/integration_tests --------------------------------------------

    private static string FixtureRoot()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
        {
            var candidate = Path.Combine(dir.FullName, "data", "integration_tests");
            if (Directory.Exists(candidate))
            {
                return candidate;
            }
        }
        throw new DirectoryNotFoundException("data/integration_tests not found above " + AppContext.BaseDirectory);
    }

    private static List<string[]> Cases(string category)
    {
        var cases = new List<string[]>();
        foreach (var file in Directory.GetFiles(Path.Combine(FixtureRoot(), category), "*.txt").OrderBy(f => f, StringComparer.Ordinal))
        {
            foreach (var line in File.ReadAllLines(file).Select(l => l.Trim()))
            {
                if (line.Length > 0 && !line.StartsWith('#'))
                {
                    cases.Add(line.Split('\t'));
                }
            }
        }
        return cases;
    }

    private static int ManifestTotal(string category)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(FixtureRoot(), "MANIFEST.json")));
        return doc.RootElement.GetProperty("totals").GetProperty(category).GetInt32();
    }

    private static double[] Numbers(string json) =>
        json.Split(new[] { '[', ']', ',', ' ' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(t => double.Parse(t, CultureInfo.InvariantCulture)).ToArray();

    private static void AssertClose(double[] expected, double[] actual)
    {
        Assert.Equal(expected.Length, actual.Length);
        for (var i = 0; i < expected.Length; i++)
        {
            Assert.True(Math.Abs(expected[i] - actual[i]) <= 1e-9, $"{expected[i]} != {actual[i]} at {i}");
        }
    }

    [Fact]
    public void FixturesFloatsEncode()
    {
        var cases = Cases("floats/encode");
        Assert.Equal(ManifestTotal("floats/encode"), cases.Count);
        foreach (var c in cases)
        {
            var precisions = Numbers(c[0]).Select(p => (int)p).ToArray();
            Assert.Equal(c[2], WkpCodec.EncodeFloats(Numbers(c[1]), precisions));
        }
    }

    [Fact]
    public void FixturesFloatsDecode()
    {
        var cases = Cases("floats/decode");
        Assert.Equal(ManifestTotal("floats/decode"), cases.Count);
        foreach (var c in cases)
        {
            var precisions = Numbers(c[0]).Select(p => (int)p).ToArray();
            AssertClose(Numbers(c[2]), WkpCodec.DecodeFloats(c[1], precisions));
        }
    }

    [Fact]
    public void FixturesGeometryEncode()
    {
        var cases = Cases("geometry/encode");
        Assert.Equal(ManifestTotal("geometry/encode"), cases.Count);
        foreach (var c in cases)
        {
            var w = Wkt.Parse(c[1]);
            var frame = new GeometryFrame(w.Type, w.Dimensions, w.Coords.ToArray(), w.Segments.ToArray(), w.Groups.ToArray(), int.Parse(c[0], CultureInfo.InvariantCulture));
            Assert.Equal(c[2], WkpCodec.EncodeFrame(frame));
        }
    }

    [Fact]
    public void FixturesGeometryDecode()
    {
        var cases = Cases("geometry/decode");
        Assert.Equal(ManifestTotal("geometry/decode"), cases.Count);
        foreach (var c in cases)
        {
            var w = Wkt.Parse(c[1]);
            var frame = WkpCodec.Decode(c[0]);
            Assert.Equal(w.Type, frame.GeometryType);
            Assert.Equal(w.Dimensions, frame.Dimensions);
            Assert.Equal(w.Segments, frame.SegmentPointCounts);
            Assert.Equal(w.Groups, frame.GroupSegmentCounts);
            AssertClose(w.Coords.ToArray(), frame.Coords);
        }
    }

    [Fact]
    public void WktParserTopology()
    {
        var w = Wkt.Parse("MULTIPOLYGON (((0 0, 1 0, 1 1, 0 0)), ((5 5, 6 5, 6 6, 5 5), (5.2 5.2, 5.4 5.2, 5.2 5.4, 5.2 5.2)))");
        Assert.Equal((GeometryType.MultiPolygon, 2), (w.Type, w.Dimensions));
        Assert.Equal(new[] { 1, 2 }, w.Groups);
        Assert.Equal(new[] { 4, 4, 4 }, w.Segments);
    }

    /// <summary>Minimal WKT reader producing the flat frame form, for the fixtures only.</summary>
    private sealed class Wkt
    {
        public GeometryType Type;
        public int Dimensions;
        public readonly List<double> Coords = new();
        public readonly List<int> Segments = new();
        public readonly List<int> Groups = new();

        private abstract record Node;
        private sealed record Coord(double[] Values) : Node;
        private sealed record Items(List<Node> Children) : Node;

        public static Wkt Parse(string text)
        {
            var open = text.IndexOf('(');
            var kind = text[..open].Trim().ToUpperInvariant();
            var pos = open;
            var root = (Items)ParseNode(text, ref pos);
            var w = new Wkt();
            switch (kind)
            {
                case "POINT":
                case "LINESTRING":
                    w.Type = kind == "POINT" ? GeometryType.Point : GeometryType.LineString;
                    w.Segment(root);
                    w.Groups.Add(1);
                    break;
                case "POLYGON":
                    w.Type = GeometryType.Polygon;
                    root.Children.ForEach(r => w.Segment((Items)r));
                    w.Groups.Add(root.Children.Count);
                    break;
                case "MULTIPOINT":
                case "MULTILINESTRING":
                    w.Type = kind == "MULTIPOINT" ? GeometryType.MultiPoint : GeometryType.MultiLineString;
                    foreach (var part in root.Children)
                    {
                        w.Segment(part as Items ?? new Items(new List<Node> { part }));
                        w.Groups.Add(1);
                    }
                    break;
                case "MULTIPOLYGON":
                    w.Type = GeometryType.MultiPolygon;
                    foreach (var polygon in root.Children.Cast<Items>())
                    {
                        polygon.Children.ForEach(r => w.Segment((Items)r));
                        w.Groups.Add(polygon.Children.Count);
                    }
                    break;
                default:
                    throw new NotSupportedException(kind);
            }
            return w;
        }

        private void Segment(Items items)
        {
            foreach (var coord in items.Children.Cast<Coord>())
            {
                Dimensions = coord.Values.Length;
                Coords.AddRange(coord.Values);
            }
            Segments.Add(items.Children.Count);
        }

        private static Node ParseNode(string s, ref int pos)
        {
            while (char.IsWhiteSpace(s[pos]))
            {
                pos++;
            }
            if (s[pos] == '(')
            {
                pos++;
                var children = new List<Node>();
                while (true)
                {
                    children.Add(ParseNode(s, ref pos));
                    while (char.IsWhiteSpace(s[pos]))
                    {
                        pos++;
                    }
                    if (s[pos++] == ')')
                    {
                        return new Items(children);
                    }
                }
            }
            var end = s.IndexOfAny(new[] { ',', ')' }, pos);
            var values = s[pos..end].Split(' ', StringSplitOptions.RemoveEmptyEntries)
                .Select(t => double.Parse(t, CultureInfo.InvariantCulture)).ToArray();
            pos = end;
            return new Coord(values);
        }
    }
}
