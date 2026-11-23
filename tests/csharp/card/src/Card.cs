using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
using Newtonsoft.Json;
using Tsugumi;
using Tsugumi.Data;
using static Tsugumi.Effects;
using NullabilityInfoContext = System.Reflection.NullabilityInfoContext;
using NullabilityState = System.Reflection.NullabilityState;

public static class Card
{
    private static Task? background;
    private static int effectCalls;

    public static Output Run(Input input)
    {
        switch (input.Params.Mode)
        {
            case 1:
                if (++effectCalls != 1 ||
                    !Task.Run(() => !Cancelled() && RemainingMilliseconds() == 500).GetAwaiter().GetResult())
                    throw new InvalidOperationException("Active effects failed");

                DisplaySamples();
                var fd = checked((nint)input.Params.Fd);
                background = Task.Run(() =>
                {
                    using var pipe = new FileStream(new SafeFileHandle(fd, ownsHandle: false), FileAccess.Read);
                    if (pipe.ReadByte() != '!' || !Cancelled() || RemainingMilliseconds() != 0 ||
                        Send("outside") != 0)
                        throw new InvalidOperationException("Between-call effects failed");
                });
                break;
            case 2:
                if (effectCalls != 1 || background is null)
                    throw new InvalidOperationException("Card state was lost");
                background.GetAwaiter().GetResult();
                break;
            case 3:
                throw new Failure(FailureKind.cancelled, "explicit failure", cell: "fit");
            case 4:
                throw new InvalidOperationException("standard exception");
            case 5:
                throw new Exception("other exception");
            case 6:
                return new Output { Number = null!, Out = new(), File = "ignored" };
            case 7:
                return new Output { Number = new() { Value = 42 }, Out = new() { Name = null }, File = "ignored" };
            case 11:
                throw new Failure(FailureKind.unspecified, "unspecified kind");
            case 12:
                throw new Failure((FailureKind)ushort.MaxValue, "unknown kind");
        }

        // A needed input: absent produces input_missing naming the port.
        var number = input.Get<inputs.number.NumberT>("number");
        var sample = input.Get<inputs.sample.SampleT>("sample");
        if (!ReferenceEquals(number, input.Number) || input.Find<string>("unwired") is not null)
            throw new InvalidOperationException("Unexpected input lookup");
        if (input.Params.Mode == 20)
            input.Get<string>("unwired");
        if (input.Params.Mode == 22)
            input.Get<string>("number");

        var count = 2;
        if (input.Params.Mode == 8)
        {
            if (sample.Name != new string('i', 40 * 1024) ||
                sample.Value.AsLabel().Value != "late input")
                throw new InvalidOperationException("Large input did not round-trip");
        }
        else
        {
            count = sample.Value.AsCount().Value;
        }
        var name = input.Params.Mode == 8 ? "table payload" : sample.Name;
        var path = input.File;
        if (path is null)
        {
            path = "created.txt";
            File.WriteAllText(path, name);
        }
        else if (File.ReadAllText(path) != "input contents")
        {
            throw new InvalidOperationException("File input could not be read");
        }

        var output = new Output
        {
            Number = new() { Value = number.Position.X + number.Position.Y + count + (input.Params.Mode == 2 ? 1 : 0) },
            Out = new()
            {
                Name = name,
                Value = outputs.@out.ValueUnion.FromLabel(new()
                {
                    Value = JsonConvert.DeserializeObject<string>("\"union result\"")!,
                }),
            },
            // Return input paths unchanged to forward them.
            File = path,
            Directory = input.Find<string>("directory"),
            Arrival = input.Arrival switch
            {
                inputs.arrival.tsugumi.merge.t0.TemperatureT temperature =>
                    new outputs.arrival.tsugumi.merge.t0.TemperatureT { Value = temperature.Value + 1 },
                inputs.arrival.tsugumi.merge.t1.ReadingT reading =>
                    new outputs.arrival.tsugumi.merge.t1.ReadingT { Name = reading.Name },
                _ => null,
            },
        };
        if (input.Params.Mode == 10)
        {
            CheckOutputTypes();
            output.Pack = new() { Items = [new() { Text = "late output" }, new() { Text = new string('o', 40 * 1024) }] };
        }
        if (input.Params.Mode == 21)
            output.Arrival = "not a Merge member";
        return output;
    }

    private static void CheckOutputTypes()
    {
        var required = typeof(Output).GetProperty(nameof(Output.Number))!;
        var optional = typeof(Output).GetProperty(nameof(Output.Pack))!;
        var nullability = new NullabilityInfoContext();
        if (!required.IsDefined(typeof(RequiredMemberAttribute), inherit: false) ||
            optional.IsDefined(typeof(RequiredMemberAttribute), inherit: false) ||
            nullability.Create(required).WriteState != NullabilityState.NotNull ||
            nullability.Create(optional).WriteState != NullabilityState.Nullable)
            throw new InvalidOperationException("Required and optional output types are incorrect");
    }

    private static void DisplaySamples()
    {
        ExpectArgumentError(() => Send("invalid\0name", []));
        ExpectArgumentError(() => Send(new Points([1], [], [1])));
        ExpectArgumentError(() => Send(new LineChart([new("id", "line", [2], [])])));
        ExpectArgumentError(() => Send(new LineChart([new("id", "line", [2], [double.NaN])])));

        byte[] bytes = [0, 13, 255];
        if (Send("résumé\n\"") != 0 ||
            Send(new Markdown("**text**")) != 0 ||
            Send(2.5) != 0 ||
            Send(new Table(["n"], [["1"]])) != 0 ||
            Send(new Image("image/png", bytes, "sample")) != 0 ||
            Send(new Points([1], [2], [-0.5f], [0x123456])) != 0 ||
            Send(new LineChart([new("id", "line", [2], [-0.5])])) != 0 ||
            Send(new Plotly("""{"data":[]}""")) != 0 ||
            Send(new VegaLite("""{"mark":"point"}""")) != 0 ||
            Send("custom", bytes) != 0)
            throw new InvalidOperationException("Display failed");
    }

    private static void ExpectArgumentError(Func<int> send)
    {
        try
        {
            send();
        }
        catch (ArgumentException)
        {
            return;
        }
        throw new InvalidOperationException("Expected a local display argument error");
    }
}
