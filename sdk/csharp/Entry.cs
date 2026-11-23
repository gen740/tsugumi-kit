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

internal sealed class InputPorts
{
    private readonly Dictionary<string, Wire.CardPort> ports = new(StringComparer.Ordinal);

    internal InputPorts(Wire.CardInput input)
    {
        for (var i = 0; i < input.InputsLength; i++)
        {
            var port = input.Inputs(i)!.Value;
            if (!ports.TryAdd(port.Name, port))
                throw Rejected(FailureKind.input_invalid, $"Duplicate input {port.Name}");
        }
    }

    internal Wire.CardPort Take(string name)
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

    internal static Wire.CardPortT FlatBufferPort(string name, byte[] bytes, List<global::tsugumi.ArtifactIdT> ids) =>
        new() { Name = name, Bytes = new List<byte>(bytes), Artifacts = ids.Count == 0 ? null : ids };

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
        byte* data, nuint length,
        NativeEffects* effects,
        void* context, delegate* unmanaged[Cdecl]<void*, byte*, nuint, int> sink)
    {
        try
        {
            Effects.Initialize(effects);
            var result = Invoke(new ReadOnlySpan<byte>(data, checked((int)length)));
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

    private static Wire.CardResultT Invoke(ReadOnlySpan<byte> bytes)
    {
        try
        {
            Input input;
            try
            {
                var envelope = Wire.CardInput.GetRootAsCardInput(new ByteBuffer(bytes.ToArray()));
                var ports = new InputPorts(envelope);
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
