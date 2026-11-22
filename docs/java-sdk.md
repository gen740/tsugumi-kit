# Writing a Java Card

This guide targets **Card ABI 9 (unreleased)**. The Card package contains
`lib/main.so` and `lib/card.jar`. The native entry point starts an embedded
Java 21 VM on the first valid call. With `reuseWorker`, the JVM and the Card's
static fields stay live for the worker's lifetime.

## Build and run

Implement a public static `run(Card.Input)` method that returns `Card.Output`.
The default entry class is `card.Main`. This example takes a required Text input
named `text` with a string field `value` and returns a required File named `file`.

```java
package card;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import tsugumi.Card;
import tsugumi.Display;

public final class Main {
    public static Card.Output run(Card.Input input) throws IOException {
        String text = input.text().getValue();
        Path file = Files.writeString(Path.of("message.txt"), text);
        Display.send(new Display.Text(text));
        return new Card.Output(file);
    }
}
```

Put the source in `src/card/Main.java` under the Card definition. Use the
[Java text template](../templates/java-write-text/) for the complete project fragment.

```nix
{ inputs, ... }:
let
  sdk = inputs.tsugumi.javaSdk;
in
{
  run = sdk.javaCard { src = ./src; };
  views = sdk.views;
}
```

The core binds the Card contract to the builder in `inputs.tsugumi`.
Outside the core, call `lib.<system>.javaCard { src; contract; }`, where
`contract` contains the generated `_contract.json`, `_inputs.fbs`,
`_outputs.fbs` and `_params.fbs`.

- Set `mainClass = "example.Main";` to choose a different package-qualified entry class.
- Pass `jars = [ "${package}/share/java/dependency.jar" ];` to put Nix-provided
  JARs on both the compile-time and runtime classpaths. Include any
  transitive JARs the Card needs. The builder runs `javac` and `jar`.
- Pass `tools = [ pkgs.coreutils ];` for executables your Card needs.
  The SDK sets `PATH` to the Card package's `bin` before starting Java.
  Use `ProcessBuilder` with the command name and its arguments. See
  [Runtime tools](../README.md#runtime-tools).

## Typed ports and parameters

The builder generates `tsugumi.Card.Input` and `tsugumi.Card.Output` records.
`input.params()` is a `parameters.ParamsT`. For FlatBuffer ports, use the
generated FlatBuffers object API. For example, a port of type
`outputs.total.Number` takes an `outputs.total.NumberT`. Set its fields
with the generated setters and pass it to the output record constructor.
Constructor arguments follow the contract's output port order. Optional input
ports are `null` when absent. Pass `null` for optional outputs you want to
omit. Required outputs must have a value. The builder appends underscores
to reserved identifiers and to record member names that collide with
`params()` or another member.

For File and Directory ports the record fields are `java.nio.file.Path`.
Inputs hold absolute paths. Create new outputs under the working directory
and finish writing before returning the paths. To forward a File or
Directory, return the input path unchanged. See the
[worker filesystem contract](cpp-sdk.md#files-directories-and-worker-lifetime).

## Display a value

`Display.send` publishes live messages. Declare the views alongside `run`
in `default.nix`, with `views = sdk.views;`.

| Value | View | Data |
| --- | --- | --- |
| `String`, `Display.Text` | `text` | Plain text |
| `Display.Markdown` | `markdown` | Markdown source |
| `Display.Table` | `table` | Column names and cells converted with `String.valueOf` |
| `Display.Image` | `image` | Bytes, media type and alt text |
| `Display.LineChart` | `line-chart` | A list of named series of `double` x and y samples |
| `Display.Points` | `points` | `float` x, y and z arrays with optional packed `0xRRGGBB` colors |

```java
Display.send(new Display.LineChart(List.of(
        new Display.LineSeries("squared", "Squared",
                new double[]{0, 1, 2}, new double[]{0, 1, 4}))));
Display.send(new Display.Points(
        new float[]{1, 0}, new float[]{0, 1}, new float[]{0, 0},
        new int[]{0xff4040, 0x40aa40}));
```

Line series require equal-length finite axes. Points require axes of equal
length and either zero colors or one color per point. The points view skips
non-finite positions.

Use `Display.send(String view, byte[] bytes)` for a custom view. Add its
`{ name; type; blob; }` entry with `views = sdk.views ++ [ ... ];`.
The view defines its payload format. Argument and encoding errors throw before
the SDK calls the host. The SDK throws `IllegalStateException` if the host
cannot accept the message. Card threads may send between invocations.
Messages are live and do not enter history. See the
[view protocol](cpp-sdk.md#display-a-value).

## Cancellation and failures

`Effects.cancelled()` and `Effects.remainingMs()` can be called from any
Card thread. An active call with no deadline reports `-1` milliseconds.
Between calls the methods return `true` and `0`, respectively.

Throw `new Failure(FailureKind.CANCELLED, "Stopped")` for a structured
failure. A third constructor argument can specify a notebook cell.
An unhandled exception or error on the invocation thread becomes a
`worker_failed` result. The SDK includes the source path and line for
matching Card stack frames when sources follow the Java package directory
layout under `src/`. Join background tasks and propagate their exceptions
to `run` when the call depends on them.

Run `nix build .#checks.<system>.javaAbi9` to exercise the Java Card through
the shared ABI fake host.
