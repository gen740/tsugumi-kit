# Rust Cards for ABI 9

Pass the Card's Cargo package directory and the core's generated contract
directory to `tsugumi-kit.lib.${system}.rustCard { src; contract; }`.
The derivation installs `lib/main.so`. The contract directory contains
`_contract.json`, `_inputs.fbs`, `_outputs.fbs`, and `_params.fbs`.

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
    send_value(input.measurement.value())?;
    let mut measurement = tsugumi::outputs::Measurement::default();
    measurement.value = input.measurement.value() * 2.0;
    Ok(Output { measurement })
}

tsugumi::export!(run);
```

The example assumes required input and output ports named `measurement`
with a `value:double` field. Inputs and parameters borrow verified
FlatBuffers for the duration of `run`. The output fields take FlatBuffers'
owned objects, including native union enums. Required outputs are fields of
their object type, and optional outputs are `Option`. Invalid output objects
produce `output_invalid`.

`tsugumi::send(view, bytes)` takes a declared view name and a byte slice.
`tsugumi::send_value(value)` supports text, `f64`, `Scalar`, `Markdown`,
`Table`, `Image`, `Points`, `LineChart`, `Plotly`, and `VegaLite` values.
Both functions return `tsugumi::Result<()>`.

The SDK at `lib.${system}.rustSdk` exposes an empty `views` list.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The example above uses the `markdown` view, which you can include from
`lib.${system}.cppSdk.views`.
See the [view protocol](../../docs/cpp-sdk.md#display-a-value).

A Card-owned thread can send displays after `run` returns. Between calls
`cancelled()` returns `true` and `remaining_ms()` returns `0`.

`Failure::new` captures its call site, and `with_cell` adds an optional
notebook cell. Set `sourcePrefix` to the Cargo package directory's path
relative to the Card definition when they differ. For example,
`sourcePrefix = "src"` reports `src/src/lib.rs` for a package in `src/`.

Read File and Directory input paths from their `value()` accessor. Write
new outputs under the working directory and return their paths in the
output object. Finish all writes before returning so the worker can snapshot
the files. Returning a File or Directory input path unchanged forwards it.
