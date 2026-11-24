#nullable enable
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
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
public enum ViewMode : uint { Panel, Window, Fullscreen }
public sealed record OpenedView(byte[] Route, byte[] Session);
public enum ReceiveStatus { Message, Close, Timeout, Cancelled }
public sealed record Received(ReceiveStatus Status, byte[] Bytes, byte[]? Route, byte[]? Session);

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeBytes
{
    internal byte* Data;
    internal nuint Size;

    internal readonly ReadOnlySpan<byte> Span => new(Data, checked((int)Size));
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEffects
{
    internal delegate* unmanaged[Cdecl]<byte*, uint, uint, uint, byte*, byte*, int> ViewOpen;
    internal delegate* unmanaged[Cdecl]<byte*, byte*, int> RouteRegister;
    internal delegate* unmanaged[Cdecl]<byte*, int> RouteUnregister;
    internal delegate* unmanaged[Cdecl]<byte*, byte*, nuint, int, int> Send;
    internal delegate* unmanaged[Cdecl]<long, byte*, byte*, NativeBytes*, int> Recv;
    internal delegate* unmanaged[Cdecl]<int> Cancelled;
    internal delegate* unmanaged[Cdecl]<long> Remaining;
    internal delegate* unmanaged[Cdecl]<byte*, NativeBytes*, int> ArtifactDir;
    internal delegate* unmanaged[Cdecl]<byte*, byte*, int> ArtifactUpload;
    internal delegate* unmanaged[Cdecl]<byte*, NativeBytes*, int> ArtifactOpen;
}

public static class Effects
{
    private static unsafe NativeEffects* effects;
    internal static unsafe void Initialize(NativeEffects* value)
    {
        if (effects == null) effects = value;
    }

    public static unsafe bool Cancelled() => effects == null || effects->Cancelled() != 0;
    public static unsafe long RemainingMilliseconds() => effects == null ? 0 : effects->Remaining();

    private static byte[] ViewName(string view)
    {
        ArgumentNullException.ThrowIfNull(view);
        if (view.Contains('\0'))
            throw new ArgumentException("View names cannot contain a NUL byte", nameof(view));
        return Encoding.UTF8.GetBytes(view + "\0");
    }

    public static unsafe OpenedView ViewOpen(string view, ViewMode mode = ViewMode.Panel,
                                             uint width = 0, uint height = 0)
    {
        var route = new byte[16];
        var session = new byte[16];
        fixed (byte* name = ViewName(view))
        fixed (byte* routePtr = route)
        fixed (byte* sessionPtr = session)
            if (effects == null || effects->ViewOpen(name, (uint)mode, width, height, routePtr, sessionPtr) != 0)
                throw new InvalidOperationException("The host could not open the view");
        return new(route, session);
    }

    private static void ValidateViewId(ReadOnlySpan<byte> id)
    {
        if (id.Length != 16)
            throw new ArgumentException("Expected a 16-byte view ID", nameof(id));
    }

    public static unsafe byte[] RouteRegister(ReadOnlySpan<byte> session)
    {
        ValidateViewId(session);
        var route = new byte[16];
        fixed (byte* sessionPtr = session)
        fixed (byte* routePtr = route)
            if (effects == null || effects->RouteRegister(sessionPtr, routePtr) != 0)
                throw new InvalidOperationException("The host could not register the route");
        return route;
    }

    public static unsafe void RouteUnregister(ReadOnlySpan<byte> route)
    {
        ValidateViewId(route);
        fixed (byte* routePtr = route)
            if (effects == null || effects->RouteUnregister(routePtr) != 0)
                throw new InvalidOperationException("The host could not unregister the route");
    }

    public static int Send(ReadOnlySpan<byte> session, ReadOnlySpan<byte> bytes) =>
        SendBytes(session, bytes, framed: false);

    private static unsafe int SendBytes(ReadOnlySpan<byte> session, ReadOnlySpan<byte> bytes, bool framed)
    {
        ValidateViewId(session);
        if (effects == null)
            throw new InvalidOperationException("View effects are not initialized");
        fixed (byte* sessionPtr = session)
        fixed (byte* data = bytes)
            return effects->Send(sessionPtr, data, (nuint)bytes.Length, framed ? 1 : 0);
    }

    public static unsafe Received Recv(long timeoutMilliseconds = -1)
    {
        if (effects == null)
            throw new InvalidOperationException("View effects are not initialized");
        NativeBytes received = default;
        byte* route = stackalloc byte[16];
        byte* session = stackalloc byte[16];
        var status = (ReceiveStatus)effects->Recv(timeoutMilliseconds, route, session, &received);
        return status switch
        {
            ReceiveStatus.Message or ReceiveStatus.Close =>
                new(status, received.Span.ToArray(), new ReadOnlySpan<byte>(route, 16).ToArray(),
                    new ReadOnlySpan<byte>(session, 16).ToArray()),
            ReceiveStatus.Timeout or ReceiveStatus.Cancelled => new(status, [], null, null),
            _ => throw new InvalidOperationException("The host could not receive a view event"),
        };
    }

    internal static unsafe string ArtifactDir(byte[] id)
    {
        NativeBytes path = default;
        fixed (byte* pointer = id)
            if (effects == null || effects->ArtifactDir(pointer, &path) != 0)
                throw new InvalidOperationException("The host could not create an Artifact");
        return Encoding.UTF8.GetString(path.Span);
    }

    internal static unsafe void ArtifactUpload(string source, byte[] id)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (source.Contains('\0'))
            throw new ArgumentException("Artifact source contains a NUL byte", nameof(source));
        fixed (byte* path = Encoding.UTF8.GetBytes(source + "\0"))
        fixed (byte* pointer = id)
            if (effects == null || effects->ArtifactUpload(path, pointer) != 0)
                throw new InvalidOperationException("Could not upload the Artifact");
    }

    internal static unsafe string ArtifactOpen(byte[] id)
    {
        NativeBytes path = default;
        fixed (byte* pointer = id)
            if (effects == null || effects->ArtifactOpen(pointer, &path) != 0)
                throw new InvalidOperationException("The host could not open the Artifact");
        return Encoding.UTF8.GetString(path.Span);
    }

    // JSON belongs only to the selected view module's config.
    private static readonly JsonSerializerOptions ViewJson = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    private static int SendJson(string view, string config) =>
        SendBytes(ViewOpen(view).Session, Encoding.UTF8.GetBytes(config), framed: true);

    private static int SendBinary(string view, byte[]? session, string config, params byte[][] parts)
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
        return SendBytes(session ?? ViewOpen(view).Session, payload, framed: true);
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
        SendBinary("image", null, JsonSerializer.Serialize(new
        {
            alt = image.Alt,
            media_type = image.MediaType,
        }, ViewJson), image.Bytes);

    public static int Send(Points points, byte[]? session = null)
    {
        var count = points.X.Length;
        var colors = points.Colors ?? [];
        if (points.Y.Length != count || points.Z.Length != count ||
            (colors.Length != 0 && colors.Length != count))
            throw new ArgumentException("Point coordinates and colors must have matching lengths", nameof(points));

        return SendBinary("points", session, JsonSerializer.Serialize(new { count }, ViewJson),
            LittleEndian(points.X, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian),
            LittleEndian(points.Y, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian),
            LittleEndian(points.Z, sizeof(float), BinaryPrimitives.WriteSingleLittleEndian),
            LittleEndian(colors, sizeof(uint), BinaryPrimitives.WriteUInt32LittleEndian));
    }

    public static int Send(LineChart chart, byte[]? session = null)
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
        return SendBinary("line-chart", session, JsonSerializer.Serialize(new { series }, ViewJson),
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
