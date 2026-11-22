# Go Card SDK

The Go builder takes the Card's source and the current wiring schemas.

```nix
{ pkgs, params, inputs, schemas }:
let
  sdk = inputs.tsugumi.goSdk;
in
{
  run = sdk.goCard { inherit schemas; src = ./src; };
  views = sdk.views ++ builtins.filter (view: view.name == "markdown") inputs.tsugumi.cppSdk.views;
}
```

The builder reads `_inputs.fbs`, `_outputs.fbs`, and `_params.fbs` and
installs the typed API and its README under `share/tsugumi/api/`.
`lib/main.so` loads the sibling `lib/go-card.so` on the first call with the
matching ABI version. Keep both libraries together.

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

A generated input accessor such as `input.Number()` returns `*T` and
yields `nil` when absent. Use `RequireInput[T](input, name)` for a
needed value. It returns `T` or an `input_missing` error naming the port.
`OptionalInput[T](input, name)` also accepts names outside the current
wiring and returns `nil` for absent values. `input.Params()` reads the
parameter object.

Table types use the FlatBuffers object API. Import generated tables, structs,
enums and unions under `tsugumi/inputs/...` and `tsugumi/outputs/...`.
A mixed Merge appears as a generated struct with a pointer to each member
type. The SDK verifies the selected member's nested FlatBuffer and sets only
that field. For a mixed Merge output, populate exactly one variant field.
The output fields take pointers and the SDK checks that required outputs are set.

`tsugumi.Send(view, bytes)` takes a declared view name and a byte slice.
`tsugumi.SendValue(value)` encodes strings, numbers, `Markdown`, `Scalar`,
`Table`, `Image`, `Points`, `LineChart`, `Plotly`, and `VegaLite` for
their corresponding views. Local argument and encoding errors are returned
before calling the host. A nonzero host status produces a transport error
indicating the host could not accept the message.

The SDK at `lib.${system}.goSdk` bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The example above uses the `markdown` view, which you can include from
`lib.${system}.cppSdk.views`.
See the [view protocol](cpp-sdk.md#display-a-value).

A goroutine may send live updates between calls. Run background work inside `tsugumi.Guard(func() error { ... })`
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

A Path is a string. Its optional representation and output field are
`*string`. Write new files and directories under the Card's working
directory and finish writing before returning. Copy an input path string
unchanged into an output field to forward it. Go package initialization runs once when the payload
loads and its state persists for the worker's lifetime. A panic in `init()`
terminates the worker. For setup that can fail, use an error-returning
`sync.OnceValue` or `sync.OnceValues` function called by `run` and propagate
the error as a Card failure.

[Card SDK contract](sdk-contract.md) · [Go template](../templates/go-normalize-text/)

## Local development

Expose the SDK generated from the same schemas in your Card's flake.

```nix
packages.${system}.goSdk = kit.lib.${system}.goCardSdk {
  inherit schemas;
  goMod = ./src/go.mod;
};
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
