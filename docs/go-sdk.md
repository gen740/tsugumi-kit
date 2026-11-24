# Go Card SDK

The Go builder takes the Card's source and the current wiring schemas.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/go> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./_inputs.fbs;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.goCard (common // { src = ./src; });
  api = sdk.goApi common;
  views = sdk.views;
}
```

`goApi` publishes the generated Go API and a typed README under
`share/tsugumi/api/`. The run output contains `lib/main.so` and
`lib/go-card.so`. Keep both libraries together.

`src` contains a Go module with a `main` package and a `go.mod` that
requires `tsugumi v0.0.0` and replaces it with `./tsugumi`.
The builder installs the generated SDK there.

```go
package main

import "tsugumi"

func run(input tsugumi.Inputs) (tsugumi.Output, error) {
    if tsugumi.Cancelled() {
        return tsugumi.Output{}, tsugumi.Fail("cancelled", "Card cancelled")
    }
    number, err := tsugumi.RequireInput[tsugumi.InputNumber](input, "number")
    if err != nil {
        return tsugumi.Output{}, err
    }
    value := number.Value * 2
    if err := tsugumi.SendValue(value); err != nil {
        return tsugumi.Output{}, err
    }
    return tsugumi.Output{Number: &tsugumi.OutputNumber{Value: value}}, nil
}
```

The SDK selects each connected input by its schema ID and validates the
payload before calling `run`. Use a generated accessor such as
`input.Number()` or look up a port with `RequireInput[T]`. A mixed Merge
appears as a struct with exactly one candidate pointer set. A Collect port
contains the fixed `Collected` table with its opaque envelope items under
`tsugumi/tsugumi/builtin`. Output fields take pointers. Leaving a
`(required)` output unset returns `output_invalid`. Optional outputs can
be omitted.

`tsugumi.Parameters()` returns a typed snapshot of the evaluated build-time
parameters. Parameter values affect the run build. The API derivation
depends on parameter types.

`tsugumi.ViewOpen(name, mode, flags, width, height)` returns a route, a session,
and an error. `tsugumi.Send(session, bytes)` sends one raw byte slice.
Use `RouteRegister(session)` to create a route and `RouteUnregister(route)`
to release it. `tsugumi.SendValue(value)` opens a panel and encodes strings,
numbers, `Markdown`, `Scalar`,
`Table`, `Image`, `Points`, `LineChart`, `Plotly`, and `VegaLite` for
their corresponding views. Local argument and encoding errors are returned
before calling the host. A nonzero host status produces a transport error
indicating the host could not accept the message.

Pass `0` for default pointer behavior or combine `tsugumi.HideCursor`
and `tsugumi.LockPointer` with `|`.

The SDK bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `markdown` view displays scalar values.
See the [view protocol](sdk-contract.md#display-and-effects).

`SendValueTo(session, value)` sends an SDK-framed value to an existing
session. `tsugumi.Recv(timeoutMS)` returns a `ViewEvent`, a `ReceiveStatus`
and an error. Status `Received` carries a message or close event with
`Route`, `Session` and `Data`. The other statuses are `Timeout` and
`CancelledReceive`. A negative timeout waits
without a deadline. A goroutine may send and receive between calls. Run background work inside `tsugumi.Guard(func() error { ... })`
in that goroutine to turn a panic into an error. Handle the error there or
send it back to `run` over a channel. An unhandled goroutine panic terminates
the worker. `Cancelled()` and `RemainingMS()` query the current
process-wide call and return `true` and `0` when no call is active.
A remaining time of `-1` means the host supplied no deadline.

Return a Go error to fail a Card. `tsugumi.Fail(kind, message)` adds the
caller's file and line when they belong to the Card module. Set
`sourcePrefix` in `goCard` if the module root is somewhere other than
`src` relative to the Card definition. Ordinary errors and panics in
`run` use `worker_failed`.

`tsugumi.Dir()` creates an Artifact and returns its writable directory.
`tsugumi.Upload(path)` copies an existing file or directory into a new
Artifact. Both keep the Artifact after its run by default. With the option
`tsugumi.Keep(false)` the Artifact exists only while its run runs: later
Cards of the same run can open it, and it is removed with its record when
the run ends. Choose keep from a param so the person running can switch it
per run, for example `keep_scratch = false;` in `params.nix` and
`tsugumi.Dir(tsugumi.Keep(tsugumi.Parameters().KeepScratch))`.

Call `artifact.Open()` to get the read-only directory of any sealed Artifact
of the project whose ID the Card knows. It returns an error when the
Artifact was not kept after its run, was deleted or is not in this project.
Use the `tsugumi.Artifact` type for generated fields and forward an input
Artifact by returning the value.
The pinned FlatBuffers backend supports vectors of tables that contain a union.
Its code generator rejects fields declared directly as a vector of unions.

Go package initialization runs once when the payload
loads and its state persists for the worker's lifetime. A panic in `init()`
terminates the worker. For setup that can fail, use an error-returning
`sync.OnceValue` or `sync.OnceValues` function called by `run` and propagate
the error as a Card failure.

[Card SDK contract](sdk-contract.md) · [Go template](../templates/go-normalize-text/)

## Local development

Expose the SDK package from the same Card build in your local development flake.
Here, `card` is the evaluated Card definition with the desired input wiring.

```nix
packages.${system}.goSdk = card.run.sdk;
```

From the flake root, link the output into your Card module. Use the same Go
version and a cgo-capable native toolchain, along with any native dependencies
your Card uses.

```sh
nix build .#goSdk --out-link src/tsugumi
cd src
go test ./...
```

Keep `src/tsugumi` out of version control and refresh the link after schema
changes. The link resolves the same types in `gopls`. `goCard` replaces any
development SDK in its build copy with the SDK generated for that build.

## Build dependencies

The builder runs offline. For dependencies fetched from a Go module proxy,
supply `lock = ./src/go.sum` and `hash` for the Nix module-proxy cache.
`buildInputs` and `nativeBuildInputs` can add cgo dependencies and build
tools.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).
