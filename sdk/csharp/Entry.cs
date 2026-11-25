#nullable enable
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Google.FlatBuffers;
using Tsugumi.Data;
using Wire = Tsugumi.Card;

namespace Tsugumi;

public sealed class Failure(
    FailureKind kind,
    string message,
    string? cell = null,
    [System.Runtime.CompilerServices.CallerFilePath] string source = "",
    [System.Runtime.CompilerServices.CallerLineNumber] int line = 0) : Exception(message)
{
    internal Wire.CardFailureT ToWire() => new()
    {
        Kind = kind != FailureKind.unspecified && Enum.IsDefined(kind) ? kind : FailureKind.worker_failed,
        Message = Message,
        Source = source.StartsWith("/card/", StringComparison.Ordinal) ? source[6..] : null,
        Line = source.StartsWith("/card/", StringComparison.Ordinal) ? (uint)line : 0,
        Cell = cell,
    };
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInput
{
    internal byte* Name;
    internal byte* Schema;
    internal byte* Data;
    internal nuint Length;
}

internal sealed class InputPorts
{
    internal sealed record Port(string? Schema, byte[] Bytes);

    private readonly Dictionary<string, Port> ports = new(StringComparer.Ordinal);

    internal unsafe InputPorts(NativeInput* inputs, nuint count)
    {
        if (inputs == null && count != 0)
            throw Rejected(FailureKind.input_invalid, "Missing input array");

        for (nuint i = 0; i < count; i++)
        {
            var input = inputs[i];
            var name = Marshal.PtrToStringUTF8((nint)input.Name);
            if (name is null || (input.Data == null && input.Length != 0))
                throw Rejected(FailureKind.input_invalid, "Invalid input port");
            var schema = Marshal.PtrToStringUTF8((nint)input.Schema);
            var bytes = new ReadOnlySpan<byte>(input.Data, checked((int)input.Length));
            if (!ports.TryAdd(name, new Port(schema, bytes.ToArray())))
                throw Rejected(FailureKind.input_invalid, $"Duplicate input {name}");
        }
    }

    internal Port Take(string name)
    {
        if (!ports.Remove(name, out var port))
            throw Rejected(FailureKind.input_missing, $"Missing input {name}");
        return port;
    }

    internal void CheckConsumed()
    {
        if (ports.Count != 0)
            throw Rejected(FailureKind.input_invalid, "Unknown input port");
    }

    internal static Failure Rejected(FailureKind kind, string message) =>
        new(kind, message, source: "", line: 0);

    internal static Failure UnknownSchema(string name) =>
        Rejected(FailureKind.input_invalid, $"Unknown schema for {name}");

    internal static Wire.CardPortT FlatBufferPort(string name, byte[] bytes) =>
        new() { Name = name, Bytes = new List<byte>(bytes) };

    internal static ByteBuffer Verify(byte[]? bytes, VerifyTableAction verifier, string name, FailureKind kind)
    {
        if (bytes is not { Length: > 0 })
            throw Rejected(kind, $"Missing FlatBuffer for {name}");

        var buffer = new ByteBuffer(bytes);
        try
        {
            if (new Verifier(buffer).VerifyBuffer(null, false, verifier))
                return buffer;
        }
        catch (Exception error) when (error is ArgumentException or IndexOutOfRangeException or OverflowException)
        {
            // Bad offsets are also exposed by the managed verifier as exceptions.
        }
        throw Rejected(kind, $"Invalid FlatBuffer for {name}");
    }

    internal static byte[] Pack(string name, VerifyTableAction verify, Func<FlatBufferBuilder, int> pack)
    {
        byte[] bytes;
        try
        {
            var builder = new FlatBufferBuilder(256);
            builder.Finish(pack(builder));
            bytes = builder.SizedByteArray();
        }
        catch (Exception error)
        {
            throw Rejected(FailureKind.output_invalid, $"Invalid output {name}: {error.Message}");
        }
        Verify(bytes, verify, name, FailureKind.output_invalid);
        return bytes;
    }
}

internal static class Entry
{
    [UnmanagedCallersOnly]
    public static unsafe int Execute(
        NativeInput* inputs, nuint inputCount,
        NativeEffects* effects,
        void* context, delegate* unmanaged[Cdecl]<void*, byte*, nuint, int> sink)
    {
        try
        {
            Effects.Initialize(effects);
            var result = Invoke(inputs, inputCount);
            var builder = new FlatBufferBuilder(256);
            builder.Finish(Wire.CardResult.Pack(builder, result).Value);
            var bytes = builder.SizedByteArray();
            fixed (byte* pointer = bytes)
                return sink(context, pointer, (nuint)bytes.Length);
        }
        catch
        {
            return 1;
        }
    }

    private static unsafe Wire.CardResultT Invoke(NativeInput* inputs, nuint inputCount)
    {
        try
        {
            Input input;
            try
            {
                var ports = new InputPorts(inputs, inputCount);
                input = new Input(ports);
                ports.CheckConsumed();
            }
            catch (Exception error) when (error is not Failure)
            {
                throw InputPorts.Rejected(FailureKind.input_invalid, error.Message);
            }

            var output = global::Card.Run(input);
            return output?.ToWire() ?? throw InputPorts.Rejected(FailureKind.output_invalid, "Missing output");
        }
        catch (Failure error)
        {
            return new() { Failure = error.ToWire() };
        }
        catch (OperationCanceledException error)
        {
            return new() { Failure = new() {
                Kind = Effects.RemainingMilliseconds() == 0 ? FailureKind.timeout : FailureKind.cancelled,
                Message = error.Message,
            } };
        }
        catch (Exception error)
        {
            return new() { Failure = new() { Kind = FailureKind.worker_failed, Message = error.Message } };
        }
    }
}
