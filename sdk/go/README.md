# Go Card SDK

Use `kit.lib.${system}.goCard { inherit src contract; }` to build a Card
containing `lib/main.so`. The core supplies the directory containing
`_contract.json`, `_inputs.fbs`, `_outputs.fbs` and `_params.fbs`.
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
    value := input.Number().Value * 2
    if err := tsugumi.Display(value); err != nil {
        return tsugumi.Output{}, err
    }
    return tsugumi.Output{Number: &tsugumi.OutputNumber{Value: value}}, nil
}
```

The generated accessors return Go FlatBuffers object types, including
`input.Params()`, and return `nil` for absent optional ports. Table,
struct, enum and union types generated for the contract are importable
under `tsugumi/inputs/...` and `tsugumi/outputs/...`. The builder
validates input and output FlatBuffers with typed verifiers.

`tsugumi.Display(value, true)` requests recording. Omitting the flag
sends a live-only display. Display accepts strings, numbers, and the SDK's
`Markdown`, `Scalar`, `Table`, `Image`, `Points`, `LineChart`,
`Plotly`, `VegaLite` and `Module` types. A goroutine may send displays
between calls. `Cancelled()` and `RemainingMS()` query the current
process-wide call and return `true` and `0` when no call is active.
A remaining time of `-1` means the host supplied no deadline.

Return a Go error to fail a Card. `tsugumi.Fail(kind, message)` adds the
caller's file and line when they belong to the Card module. Set
`sourcePrefix` in `goCard` if the module root is somewhere other than
`src` relative to the Card definition. Ordinary errors and panics in
`run` use `worker_failed`.

Finish writing files and directories before returning from `run`. Create
outputs under the Card's working directory and return their paths in
the corresponding fields. To forward a File or Directory, return its
input path unchanged. The Go runtime and package state live in the worker
process. To defer Card-specific setup until the first call, use
`sync.Once` or `sync.OnceValue` inside `run`.

The builder runs offline. For dependencies fetched from a Go module proxy,
supply `lock = ./src/go.sum` and `hash` for the Nix module-proxy cache.
`buildInputs` and `nativeBuildInputs` can add cgo dependencies and build
tools.
