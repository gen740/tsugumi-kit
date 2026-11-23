# Rust Card SDK

The Rust builder takes the Card's Cargo package and the current wiring schemas.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/rust> { inherit pkgs; };
  common = {
    inherit params;
    inputs = if builtins.pathExists ./_inputs.fbs then ./_inputs.fbs else null;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.rustCard (common // { src = ./src; sourcePrefix = "src"; });
  api = sdk.rustApi common;
  views = sdk.views;
}
```

`rustApi` publishes the generated Rust API and a typed README under
`share/tsugumi/api/`. The run output contains `lib/main.so`.

Use a Cargo library package with `crate-type = ["staticlib"]` and
`tsugumi = { path = "tsugumi" }` in `[dependencies]`. The builder places a
generated SDK crate at that path and exposes it as the derivation's `sdk`
attribute for use outside the build. The built-in lock covers the SDK's
dependencies. For other registry dependencies, generate a Cargo lockfile
with the generated SDK in place and pass it as `lock`. The builder then
uses that lockfile for the offline build.
Supply system libraries needed by Cargo dependencies in `buildInputs`.
The builder uses Cargo and rustc's native link requirements when linking
`main.so`.

```rust
use tsugumi::{Failure, FailureKind, Inputs, Output, cancelled, send_value};

fn run(input: Inputs<'_>) -> Result<Output, Failure> {
    if cancelled() {
        return Err(Failure::new(FailureKind::cancelled, "Processing stopped"));
    }
    let value = input.measurement.value();
    send_value(value)?;
    let mut measurement = tsugumi::outputs::Measurement::default();
    measurement.value = value * 2.0;
    Ok(Output { measurement: Some(measurement) })
}

tsugumi::export!(run);
```

The example uses a connected input and a required output named `measurement`
with a `value:double` field. Inputs borrow verified FlatBuffers for the
duration of `run`. A mixed Merge is an enum in `tsugumi::inputs` whose variants
contain the borrowed table selected by the envelope's schema ID. A Collect
port borrows the fixed `Collected` table of opaque envelope items. Outputs
use owned FlatBuffers objects in `Option` fields. Missing required outputs
and invalid output objects produce `output_invalid`.

`tsugumi::PARAMS` is a typed build-time constant. A parameter value change
affects the run build and preserves the generated API when its type is
unchanged. The entry also exports the shared `sdk.input` and `sdk.artifact`
helpers for Input Cards.

`tsugumi::send(view, bytes)` takes a declared view name and a byte slice.
`tsugumi::send_value(value)` supports text, `f64`, `Scalar`, `Markdown`,
`Table`, `Image`, `Points`, `LineChart`, `Plotly`, and `VegaLite` values.
Both functions return `tsugumi::Result<()>`. Local argument and encoding
errors return a `Failure` with kind `invalid_request` before calling the host.
A nonzero host status returns `worker_failed` and includes the transport status.
The core forwards the view name and the UI looks up the corresponding declared view.

The SDK bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `markdown` view displays scalar values.
See the [view protocol](cpp-sdk.md#display-a-value).

The SDK frames and splits sends at the ABI message limit.
`tsugumi::recv(view, timeout_ms)` returns `Received::Message(bytes)`,
`Received::Timeout`, or `Received::Cancelled`. A negative timeout waits
without a deadline. A Card-owned thread can send and receive after `run`
returns. Between calls
`cancelled()` returns `true` and `remaining_ms()` returns `0`.

`Failure::new` captures its call site, and `with_cell` adds an optional
notebook cell. Set `sourcePrefix` to the Cargo package directory's path
relative to the Card definition when they differ. For example,
`sourcePrefix = "src"` reports `src/src/lib.rs` for a package in `src/`.

Use `Artifact::dir()` for an Artifact and its writable directory or
`Artifact::upload(source)` to copy a file or directory into a new Artifact.
Call `artifact.open()` during the invocation to get its granted read-only
directory. To forward a borrowed Artifact, return `artifact.unpack()`.
The SDK lists IDs from nested tables, vectors and unions in each output.
The pinned FlatBuffers backend supports vectors of tables that contain a union.
Its code generator rejects fields declared directly as a vector of unions.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).

[Card SDK contract](sdk-contract.md) · [Rust template](../templates/rust-number-lines/)
