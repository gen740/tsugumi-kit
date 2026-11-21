#nullable enable
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Text.Encodings.Web;
using System.Text.Json;
using Google.FlatBuffers;
using Wire = Tsugumi.Card;

namespace Tsugumi;

public sealed record Markdown(string Text);
public sealed record Scalar(double Value, string Label = "");
public sealed record Table(string[] Columns, string[][] Rows);
public sealed record Image(string MediaType, byte[] Bytes, string Alt = "");
public sealed record Points(float[] X, float[] Y, float[] Z, uint[]? Colors = null);
public sealed record Series(string Id, string Label, double[] X, double[] Y);
public sealed record LineChart(Series[] Series);
public sealed record Plotly(string Config);
public sealed record VegaLite(string Config);
public sealed record Blob(string Name, byte[] Bytes);
public sealed record Module(string Id, string Config, Blob[]? Blobs = null);

public static class Effects
{
    private static unsafe delegate* unmanaged[Cdecl]<byte*, nuint, int, int> send;
    private static unsafe delegate* unmanaged[Cdecl]<int> queryCancelled;
    private static unsafe delegate* unmanaged[Cdecl]<long> queryRemaining;

    internal static unsafe void Initialize(
        delegate* unmanaged[Cdecl]<byte*, nuint, int, int> display,
        delegate* unmanaged[Cdecl]<int> cancelled,
        delegate* unmanaged[Cdecl]<long> remaining)
    {
        if (send != null) return;
        queryCancelled = cancelled;
        queryRemaining = remaining;
        send = display;
    }

    public static unsafe bool Cancelled() => queryCancelled == null || queryCancelled() != 0;
    public static unsafe long RemainingMilliseconds() => queryRemaining == null ? 0 : queryRemaining();

    public static unsafe int Display(Module module, bool record = false)
    {
        if (send == null) return 1;
        var builder = new FlatBufferBuilder(256);
        var blobs = (module.Blobs ?? []).Select(blob => new Wire.CardDisplayBlobT
        {
            Name = blob.Name,
            Bytes = new List<byte>(blob.Bytes),
        }).ToList();
        builder.Finish(Wire.CardDisplay.Pack(builder, new()
        {
            Module = module.Id,
            Config = module.Config,
            Blobs = blobs,
        }).Value);
        var bytes = builder.SizedByteArray();
        fixed (byte* pointer = bytes)
            return send(pointer, (nuint)bytes.Length, record ? 1 : 0);
    }

    // JSON belongs only to the selected view module's config.
    private static readonly JsonSerializerOptions ViewJson = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    public static int Display(string text, bool record = false) =>
        Display(new Module("tsugumi:text", JsonSerializer.Serialize(new { text }, ViewJson)), record);

    public static int Display(Markdown markdown, bool record = false) =>
        Display(new Module("tsugumi:markdown", JsonSerializer.Serialize(new { text = markdown.Text }, ViewJson)), record);

    public static int Display(double value, bool record = false) => Display(new Scalar(value), record);

    public static int Display(Scalar scalar, bool record = false) =>
        Display(new Module("tsugumi:scalar", JsonSerializer.Serialize(new
        {
            label = scalar.Label,
            value = double.IsFinite(scalar.Value) ? (double?)scalar.Value : null,
        }, ViewJson)), record);

    public static int Display(Table table, bool record = false) =>
        Display(new Module("tsugumi:table", JsonSerializer.Serialize(new
        {
            columns = table.Columns,
            rows = table.Rows,
        }, ViewJson)), record);

    public static int Display(Image image, bool record = false) =>
        Display(new Module("tsugumi:image", JsonSerializer.Serialize(new
        {
            alt = image.Alt,
            media_type = image.MediaType,
        }, ViewJson), [new("image", image.Bytes)]), record);

    public static int Display(Points points, bool record = false)
    {
        var count = points.X.Length;
        var colors = points.Colors ?? [];
        if (points.Y.Length != count || points.Z.Length != count ||
            (colors.Length != 0 && colors.Length != count))
            return 1;

        return Display(new Module("tsugumi:points", JsonSerializer.Serialize(new { count }, ViewJson),
        [
            new("x", LittleEndian(points.X, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian)),
            new("y", LittleEndian(points.Y, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian)),
            new("z", LittleEndian(points.Z, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian)),
            new("colors", LittleEndian(colors, sizeof(uint), BinaryPrimitives.WriteUInt32LittleEndian)),
        ]), record);
    }

    public static int Display(LineChart chart, bool record = false)
    {
        var offset = 0;
        var series = new List<ChartSeries>();
        foreach (var row in chart.Series)
        {
            if (row.X.Length != row.Y.Length ||
                !row.X.All(double.IsFinite) || !row.Y.All(double.IsFinite))
                return 1;
            series.Add(new(row.X.Length, row.Id, row.Label, offset));
            offset = checked(offset + row.X.Length);
        }
        var x = chart.Series.SelectMany(row => row.X).ToArray();
        var y = chart.Series.SelectMany(row => row.Y).ToArray();
        return Display(new Module("tsugumi:line-chart", JsonSerializer.Serialize(new { series }, ViewJson),
        [
            new("x", LittleEndian(x, sizeof(double), BinaryPrimitives.WriteDoubleLittleEndian)),
            new("y", LittleEndian(y, sizeof(double), BinaryPrimitives.WriteDoubleLittleEndian)),
        ]), record);
    }

    public static int Display(Plotly plot, bool record = false) =>
        Display(new Module("tsugumi:plotly", plot.Config), record);

    public static int Display(VegaLite plot, bool record = false) =>
        Display(new Module("tsugumi:vega-lite", plot.Config), record);

    private sealed record ChartSeries(int count, string id, string label, int offset);
    private delegate void WriteValue<T>(Span<byte> bytes, T value);

    private static byte[] LittleEndian<T>(T[] values, int size, WriteValue<T> write)
    {
        var bytes = new byte[checked(values.Length * size)];
        for (var i = 0; i < values.Length; i++)
            write(bytes.AsSpan(i * size, size), values[i]);
        return bytes;
    }
}
