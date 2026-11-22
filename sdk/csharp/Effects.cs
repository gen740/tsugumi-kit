#nullable enable
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;

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
public static class Effects
{
    private static unsafe delegate* unmanaged[Cdecl]<byte*, byte*, nuint, int> send;
    private static unsafe delegate* unmanaged[Cdecl]<int> queryCancelled;
    private static unsafe delegate* unmanaged[Cdecl]<long> queryRemaining;

    internal static unsafe void Initialize(
        delegate* unmanaged[Cdecl]<byte*, byte*, nuint, int> sendDisplay,
        delegate* unmanaged[Cdecl]<int> cancelled,
        delegate* unmanaged[Cdecl]<long> remaining)
    {
        if (send != null) return;
        queryCancelled = cancelled;
        queryRemaining = remaining;
        send = sendDisplay;
    }

    public static unsafe bool Cancelled() => queryCancelled == null || queryCancelled() != 0;
    public static unsafe long RemainingMilliseconds() => queryRemaining == null ? 0 : queryRemaining();

    public static unsafe int Send(string view, ReadOnlySpan<byte> bytes)
    {
        ArgumentNullException.ThrowIfNull(view);
        if (view.Contains('\0'))
            throw new ArgumentException("View names cannot contain a NUL byte", nameof(view));
        if (send == null)
            throw new InvalidOperationException("Display is not initialized");
        var name = Encoding.UTF8.GetBytes(view + "\0");
        fixed (byte* viewPointer = name)
        fixed (byte* data = bytes)
            return send(viewPointer, data, (nuint)bytes.Length);
    }

    // JSON belongs only to the selected view module's config.
    private static readonly JsonSerializerOptions ViewJson = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    private static int SendJson(string view, string config) =>
        Send(view, Encoding.UTF8.GetBytes(config));

    private static int SendBinary(string view, string config, params byte[][] parts)
    {
        var metadata = Encoding.UTF8.GetBytes(config);
        var payload = new byte[checked(sizeof(uint) + metadata.Length + parts.Sum(part => part.Length))];
        BinaryPrimitives.WriteUInt32LittleEndian(payload, (uint)metadata.Length);
        metadata.CopyTo(payload, sizeof(uint));
        var offset = sizeof(uint) + metadata.Length;
        foreach (var part in parts)
        {
            part.CopyTo(payload, offset);
            offset += part.Length;
        }
        return Send(view, payload);
    }

    public static int Send(string text) =>
        SendJson("text", JsonSerializer.Serialize(new { text }, ViewJson));

    public static int Send(Markdown markdown) =>
        SendJson("markdown", JsonSerializer.Serialize(new { text = markdown.Text }, ViewJson));

    public static int Send(double value) => Send(new Scalar(value));

    public static int Send(Scalar scalar) =>
        Send(new Markdown(
            (scalar.Label.Length == 0 ? "" : $"**{scalar.Label}**: ") +
            (double.IsFinite(scalar.Value)
                ? scalar.Value.ToString("G6", System.Globalization.CultureInfo.InvariantCulture)
                : "\u2014")));

    public static int Send(Table table) =>
        SendJson("table", JsonSerializer.Serialize(new
        {
            columns = table.Columns,
            rows = table.Rows,
        }, ViewJson));

    public static int Send(Image image) =>
        SendBinary("image", JsonSerializer.Serialize(new
        {
            alt = image.Alt,
            media_type = image.MediaType,
        }, ViewJson), image.Bytes);

    public static int Send(Points points)
    {
        var count = points.X.Length;
        var colors = points.Colors ?? [];
        if (points.Y.Length != count || points.Z.Length != count ||
            (colors.Length != 0 && colors.Length != count))
            throw new ArgumentException("Point coordinates and colors must have matching lengths", nameof(points));

        return SendBinary("points", JsonSerializer.Serialize(new { count }, ViewJson),
            LittleEndian(points.X, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian),
            LittleEndian(points.Y, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian),
            LittleEndian(points.Z, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian),
            LittleEndian(colors, sizeof(uint), BinaryPrimitives.WriteUInt32LittleEndian));
    }

    public static int Send(LineChart chart)
    {
        var offset = 0;
        var series = new List<ChartSeries>();
        foreach (var row in chart.Series)
        {
            if (row.X.Length != row.Y.Length ||
                !row.X.All(double.IsFinite) || !row.Y.All(double.IsFinite))
                throw new ArgumentException("Line coordinates must be finite and have matching lengths", nameof(chart));
            series.Add(new(row.X.Length, row.Id, row.Label, offset));
            offset = checked(offset + row.X.Length);
        }
        var x = chart.Series.SelectMany(row => row.X).ToArray();
        var y = chart.Series.SelectMany(row => row.Y).ToArray();
        return SendBinary("line-chart", JsonSerializer.Serialize(new { series }, ViewJson),
            LittleEndian(x, sizeof(double), BinaryPrimitives.WriteDoubleLittleEndian),
            LittleEndian(y, sizeof(double), BinaryPrimitives.WriteDoubleLittleEndian));
    }

    public static int Send(Plotly plot) =>
        SendJson("plotly", plot.Config);

    public static int Send(VegaLite plot) =>
        SendJson("vega-lite", plot.Config);

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
