# Rust Cards for ABI 9

The Rust builder takes the Card's Cargo package and the current wiring schemas.

```nix
{ pkgs, params, inputs, schemas }:
let
  sdk = inputs.tsugumi.rustSdk;
in
{
  run = sdk.rustCard { inherit schemas; src = ./src; sourcePrefix = "src"; };
  views = sdk.views;
}
```

The builder reads `_inputs.fbs`, `_outputs.fbs`, and `_params.fbs` and
installs `lib/main.so` and the typed API with its README under
`share/tsugumi/api/`.

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
use tsugumi::{Failure, FailureKind, Inputs, Output, cancelled, require_input, send_value};

fn run(input: Inputs<'_>) -> Result<Output, Failure> {
    if cancelled() {
        return Err(Failure::new(FailureKind::cancelled, "Processing stopped"));
    }
    let value = require_input("measurement", input.measurement)?.value();
    send_value(value)?;
    let mut measurement = tsugumi::outputs::Measurement::default();
    measurement.value = value * 2.0;
    Ok(Output { measurement })
}

tsugumi::export!(run);
```

The example uses a connected input and a required output named `measurement`
with a `value:double` field. Input fields are `Option<T>`. Pass a port name
and its option to `require_input(name, option)?` to get `T` and report
`input_missing` for a missing value. Inputs and parameters borrow verified
FlatBuffers for the duration of `run`.

A mixed Merge is an enum in `tsugumi::inputs` or `tsugumi::outputs`.
Its variants contain the member type, borrowed for inputs and owned for
outputs. The SDK verifies and decodes the nested FlatBuffer for the selected
input member. Other output fields take FlatBuffers' owned objects. Optional
outputs use `Option`. Invalid output objects produce `output_invalid`.

`tsugumi::send(view, bytes)` takes a declared view name and a byte slice.
`tsugumi::send_value(value)` supports text, `f64`, `Scalar`, `Markdown`,
`Table`, `Image`, `Points`, `LineChart`, `Plotly`, and `VegaLite` values.
Both functions return `tsugumi::Result<()>`. Local argument and encoding
errors return a `Failure` with kind `invalid_request` before calling the host.
A nonzero host status returns `worker_failed` and includes the transport status.
The core forwards the view name and the UI looks up the corresponding declared view.

The SDK at `lib.${system}.rustSdk` bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `markdown` view displays scalar values.
See the [view protocol](cpp-sdk.md#display-a-value).

A Card-owned thread can send displays after `run` returns. Between calls
`cancelled()` returns `true` and `remaining_ms()` returns `0`.

`Failure::new` captures its call site, and `with_cell` adds an optional
notebook cell. Set `sourcePrefix` to the Cargo package directory's path
relative to the Card definition when they differ. For example,
`sourcePrefix = "src"` reports `src/src/lib.rs` for a package in `src/`.

Path inputs are `Option<&Path>`. Use the generated field or
`input.path(name)`, which also accepts unwired names. Output paths are
`PathBuf`. Write new output files and directories under the working directory
and finish writing before returning. Use `path.to_owned()` to forward an
input path unchanged.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).

[Card SDK contract](sdk-contract.md) · [Rust template](../templates/rust-number-lines/)
