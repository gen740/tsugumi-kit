# Writing a Java Card

This guide targets **the Card ABI (unreleased)**. The Card package contains
`lib/main.so` and `lib/card.jar`. The native entry point starts an embedded
Java 21 VM on the first valid call. With `reuseWorker`, the JVM and the Card's
static fields stay live for the worker's lifetime.

## Build and run

Implement a public static `run(Card.Input)` method that returns `Card.Output`.
The default entry class is `card.Main`. This example needs a `text` input
wired from a Text table with a string field `value`. It returns an Artifact on `file`.

```java
package card;

import java.io.IOException;
import java.nio.file.Files;
import tsugumi.Artifacts;
import tsugumi.Card;
import tsugumi.Display;

public final class Main {
    public static Card.Output run(Card.Input input) throws IOException {
        String text = Card.params().value();
        var created = Artifacts.dir();
        Files.writeString(created.path().resolve("message.txt"), text);
        Display.send(new Display.Text(text));
        return new Card.Output(created.artifact());
    }
}
```

Put the source in `src/card/Main.java` under the Card definition. Use the
[Java text template](../templates/java-write-text/) for the complete project fragment.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/java> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./_inputs.fbs;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.javaCard (common // { src = ./src; });
  api = sdk.javaApi common;
  views = sdk.views;
}
```

`javaApi` publishes `tsugumi/Card.java`, the port bindings, and a typed README
under `share/tsugumi/api/`. Use **Build SDK** in the Card editor to inspect
the API for the current wiring. [Card SDK contract](sdk-contract.md)

- Set `mainClass = "example.Main";` to choose a different package-qualified entry class.
- Pass `jars = [ "${package}/share/java/dependency.jar" ];` to put Nix-provided
  JARs on both the compile-time and runtime classpaths. Include any
  transitive JARs the Card needs. The builder runs `javac` and `jar`.
- Pass `tools = [ pkgs.coreutils ];` for executables your Card needs.
  The SDK sets `PATH` to the Card package's `bin` before starting Java.
  Use `ProcessBuilder` with the command name and its arguments. See
  [Runtime tools](../README.md#runtime-tools).

## Typed ports and parameters

The SDK generates typed accessors such as `input.text()` for connected
ports. It selects the table by the envelope's schema ID and verifies its
payload before invoking the Card. A mixed Merge uses a sealed interface
with a record variant for each candidate. Use pattern matching on those
variants. A Collect port exposes the fixed `tsugumi.builtin.CollectedT`
table of opaque envelope items.

The `Card.Output` record takes FlatBuffers object types, such as
`outputs.total.NumberT`, in output port order. A missing `(required)`
output produces `output_invalid`. Pass `null` to omit an optional output.

`Card.params()` returns the typed build-time parameter record. Parameter
values affect the run derivation and preserve the API when their types are
unchanged.

`Artifacts.dir()` returns a writable directory and its `ArtifactT`.
`Artifacts.upload(path)` copies a file or directory into a new Artifact.
Call `artifact.open()` for the granted read-only directory. Return the
same object to forward an Artifact. The SDK lists IDs from nested tables,
vectors and unions in each output.

## Display a value

Sending a typed value opens its bundled view as a panel. Declare the views alongside `run`
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

Use `Display.viewOpen(name)` to open a declared custom view, then
`Display.send(opened.session(), bytes)` to send a raw `byte[]` message. Add its
`{ name; type; blob; }` entry with `views = sdk.views ++ [ ... ];`.
Use `Display.routeRegister(session)` and `Display.routeUnregister(route)`
to manage routes. The SDK frames typed values. `Points` and `LineChart`
sends also accept an existing session. `Display.recv(timeoutMs)` returns
a `Received` with status `MESSAGE`, `CLOSE`, `TIMEOUT` or `CANCELLED`.
Message and close events carry the route and session. A negative timeout
waits without a deadline. Card threads can send and receive between
invocations. See the [view protocol](sdk-contract.md#display-and-effects).

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

Run `nix build .#checks.<system>.javaAbi` to exercise the Java Card through
the shared ABI fake host.
