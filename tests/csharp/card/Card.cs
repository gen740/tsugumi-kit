#nullable enable
using System;
using System.IO;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
using Tsugumi;
using Tsugumi.Data;
using static Tsugumi.Effects;

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
                return new Output();
            case 7:
                return new Output { Number = new() { Value = 42 }, Out = new() { Name = null }, File = new() { Value = "ignored" } };
        }

        var count = input.Sample.Value.AsCount().Value;
        var path = input.File?.Value;
        if (path is null)
        {
            path = "created.txt";
            File.WriteAllText(path, input.Sample.Name);
        }
        else if (File.ReadAllText(path) != "input contents")
        {
            throw new InvalidOperationException("File input could not be read");
        }

        return new Output
        {
            Number = new() { Value = input.Number.Value + count + (input.Params.Mode == 2 ? 1 : 0) },
            Out = new()
            {
                Name = input.Sample.Name,
                Value = outputs.@out.ValueUnion.FromLabel(new() { Value = "union result" }),
            },
            File = new() { Value = path },
            Directory = input.Directory is { } directory ? new() { Value = directory.Value } : null,
        };
    }

    private static void DisplaySamples()
    {
        byte[] bytes = [0, 13, 255];
        if (Display("résumé\n\"") != 0 ||
            Display(new Markdown("**text**"), record: true) != 0 ||
            Display(2.5) != 0 ||
            Display(new Table(["n"], [["1"]])) != 0 ||
            Display(new Image("image/png", bytes, "sample")) != 0 ||
            Display(new Points([1], [1], [1], [0x123456])) != 0 ||
            Display(new LineChart([new("id", "line", [2], [2])])) != 0 ||
            Display(new Plotly("""{"data":[]}""")) != 0 ||
            Display(new VegaLite("""{"mark":"point"}""")) != 0 ||
            Display(new Module("custom", """{"key":1}""", [new("sample", bytes)]), record: true) != 0)
            throw new InvalidOperationException("Display failed");
    }
}
