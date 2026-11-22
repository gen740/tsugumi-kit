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
                var fd = input.Params.Fd;
                background = Task.Run(() =>
                {
                    using var pipe = new FileStream(new SafeFileHandle(fd, ownsHandle: false), FileAccess.Read);
                    if (pipe.ReadByte() != '!' || !Cancelled() || RemainingMilliseconds() != 0 ||
                        Display("outside") != 0)
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
                return new Output { Number = null!, Out = new(), File = new() { Value = "ignored" } };
            case 7:
                return new Output { Number = new() { Value = 42 }, Out = new() { Name = null }, File = new() { Value = "ignored" } };
            case 11:
                throw new Failure(FailureKind.unspecified, "unspecified kind");
            case 12:
                throw new Failure((FailureKind)ushort.MaxValue, "unknown kind");
        }

        var count = 2;
        if (input.Params.Mode == 8)
        {
            if (input.Sample.Name != new string('i', 40 * 1024) ||
                input.Sample.Value.AsLabel().Value != "late input")
                throw new InvalidOperationException("Large input did not round-trip");
        }
        else
        {
            count = input.Sample.Value.AsCount().Value;
        }
        var name = input.Params.Mode == 8 ? "table payload" : input.Sample.Name;
        string? path = input.File?.Value;
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
            Number = new() { Value = input.Number.Position.X + input.Number.Position.Y + count + (input.Params.Mode == 2 ? 1 : 0) },
            Out = new()
            {
                Name = name,
                Value = outputs.@out.ValueUnion.FromLabel(new()
                {
                    Value = JsonConvert.DeserializeObject<string>("\"union result\"")!,
                }),
            },
            File = new() { Value = path },
            Directory = input.Directory is { } directory ? new() { Value = directory.Value } : null,
        };
        if (input.Params.Mode == 10)
        {
            CheckOutputTypes();
            output.Pack_ = new() { Items = [new() { Text = "late output" }, new() { Text = new string('o', 40 * 1024) }] };
        }
        return output;
    }

    private static void CheckOutputTypes()
    {
        var required = typeof(Output).GetProperty(nameof(Output.Number))!;
        var optional = typeof(Output).GetProperty(nameof(Output.Pack_))!;
        var nullability = new NullabilityInfoContext();
        if (!required.IsDefined(typeof(RequiredMemberAttribute), inherit: false) ||
            optional.IsDefined(typeof(RequiredMemberAttribute), inherit: false) ||
            nullability.Create(required).WriteState != NullabilityState.NotNull ||
            nullability.Create(optional).WriteState != NullabilityState.Nullable)
            throw new InvalidOperationException("Required and optional output types are incorrect");
    }

    private static void DisplaySamples()
    {
        byte[] bytes = [0, 13, 255];
        if (Display("résumé\n\"") != 0 ||
            Display(new Markdown("**text**"), record: true) != 0 ||
            Display(2.5) != 0 ||
            Display(new Table(["n"], [["1"]])) != 0 ||
            Display(new Image("image/png", bytes, "sample")) != 0 ||
            Display(new Points([1], [2], [-0.5f], [0x123456])) != 0 ||
            Display(new LineChart([new("id", "line", [2], [-0.5])])) != 0 ||
            Display(new Plotly("""{"data":[]}""")) != 0 ||
            Display(new VegaLite("""{"mark":"point"}""")) != 0 ||
            Display(new Module("custom", [new("config", Encoding.UTF8.GetBytes("""{"key":1}""")), new("sample", bytes)]), record: true) != 0)
            throw new InvalidOperationException("Display failed");
    }
}
