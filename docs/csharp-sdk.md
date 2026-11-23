# C# Card SDK

This guide targets **the Card ABI (unreleased)**.

Import the C# SDK in your Card's `default.nix` and pass the C# sources as `src`.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/csharp> { inherit pkgs; };
  common = {
    inherit params;
    inputs = if builtins.pathExists ./_inputs.fbs then ./_inputs.fbs else null;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.csharpCard (common // { src = ./src; });
  api = sdk.csharpApi common;
  views = sdk.views;
}
```

For an input wired from an output of type `thermometer.Temperature` with a
`value:double` field, and an output `fahrenheit` of the same table, put this
in `src/Card.cs`:

```csharp
using Tsugumi;
using static Tsugumi.Effects;

public static class Card
{
    public static Output Run(Input input)
    {
        var celsius = input.Get<inputs.celsius.thermometer.TemperatureT>("celsius");
        var fahrenheit = celsius.Value * 1.8 + 32.0;
        Send(new Scalar(fahrenheit, "°F"));
        return new Output { Fahrenheit = new() { Value = fahrenheit } };
    }
}
```

The [C# note template](../templates/csharp-write-note/) gives a complete
definition that writes a parameter to a file in an Artifact.

`csharpApi` publishes `Card.g.cs`, the FlatBuffers bindings under `Ports/`,
and a typed README under `share/tsugumi/api/`. Use **Build SDK** in the Card
editor to inspect the API for the current wiring. [Card SDK contract](sdk-contract.md)

The SDK generates typed accessors such as `input.Celsius` for connected
ports. It selects a candidate by the envelope's schema ID and verifies its
payload before invoking the Card. A mixed Merge has a generated record
variant for every possible member. Use pattern matching on
`Input_arrival.Number` and the other candidate variants. A Collect port
contains the fixed `tsugumi.builtin.CollectedT` table of opaque envelope
items.

`Output` has one nullable property per port. An omitted `(required)`
output produces `output_invalid`. Output values use the generated
FlatBuffers object types, such as `TemperatureT`.

`Params.Current` exposes the typed build-time parameter record.
Parameter values affect the run derivation and preserve the API when their
types are unchanged. The SDK also exports the shared `sdk.input` and
`sdk.artifact` helpers for Input Cards.

`tsugumi.ArtifactT.Dir()` creates an Artifact and returns its writable
directory. `ArtifactT.Upload(source)` copies a file or directory into a new
Artifact. Call `artifact.Open()` to access the granted read-only directory.
Return the same object to forward it. The SDK lists IDs from nested tables,
vectors and unions in each output.

`Effects.Send(view, bytes)` takes a view name and a
`ReadOnlySpan<byte>`. The `Effects.Send(value)` overloads support text,
scalar values, markdown, tables, images, points, line charts, Plotly and
Vega-Lite. Local argument and encoding errors throw before the host is called.
The return value is zero when the host accepts the update. A nonzero status
means the host could not accept the message.

The C# SDK bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `markdown` view displays scalar values.
See the [view protocol](cpp-sdk.md#display-a-value).

The SDK frames and splits sends at the ABI message limit.
`Effects.Recv(view, timeoutMilliseconds)` reports a message, timeout or
cancellation. A negative timeout waits without a deadline. Background
threads can send and receive between calls.
`Effects.Cancelled()` and `Effects.RemainingMilliseconds()` can be queried
from any thread. Between calls they return `true` and `0`, respectively.
An active call with no deadline can report `-1`.

Throw `new Failure(FailureKind.cancelled, "stopped")` to return a typed
failure. `FailureKind` is in `Tsugumi.Data`. The SDK includes the source
file and line and accepts an optional `cell`. Ordinary exceptions and invalid
`FailureKind` values map to `worker_failed`. An `OperationCanceledException`
maps to `cancelled`, or to `timeout` when no time remains.

The builder defaults to Nixpkgs' .NET 10 SDK and permits a `dotnet` override.
It returns a derivation containing `lib/main.so` and retains the managed Card
and runtime as Nix dependencies. A worker can set `DOTNET_EnableDiagnostics=0`
before launch to disable .NET diagnostic sockets in a restricted environment.
For external NuGet dependencies, supply both `lock = ./packages.lock.json`
and `hash` (the NAR hash of the package feed defined by the lock). Use
`lib.fakeHash` to get the expected hash from the first build. Restore uses
the locked local feed in the build sandbox.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).
