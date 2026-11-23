# Julia SDK for the Card ABI

Put `main(input)` in `src/main.jl`. The SDK embeds Julia in the Card's
`lib/main.so`.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/julia> { inherit pkgs; };
  common = {
    inherit params;
    inputs = if builtins.pathExists ./_inputs.fbs then ./_inputs.fbs else null;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.juliaCard (common // { src = ./src; });
  api = sdk.juliaApi common;
  views = sdk.views;
}
```

`juliaApi` publishes `card.jl`, the FlatBuffers schemas, and a typed README
under `share/tsugumi/api/`. Set `entry` to choose another file relative to `src`.

```julia
function main(input::Inputs)
    cancelled() && throw(CardFailure(:cancelled, "Stopped before writing"))
    file, directory = dir(Artifact)
    open(io -> println(io, input.text["value"]), joinpath(directory, "note.txt"), "w")
    return (; file)
end
```

The [Julia starter](../templates/julia-write-text/) uses this entry with a
typed `text` input and an Artifact `file` output.

The SDK also exposes `sdk.input` and `sdk.artifact` for Input Card
definitions. See the [Card SDK contract](sdk-contract.md).

## Inputs and outputs

Read a connected input with `input.text` or `input[:text]`. Use
`get(input, :text, nothing)` to handle a disconnected port. Read build-time
parameters from the `params` named tuple. A list param is a tuple, and
integer params use `Int64`.

A mixed Merge yields a dictionary with `"type"` and `"value"` entries.
The SDK selects and verifies the incoming member by schema ID. Collect
yields a table with opaque envelopes containing `"schema"`, `"bytes"`
and `"artifacts"`. Return a named tuple or dictionary of outputs. Omit
optional outputs or use `nothing`. A missing required output produces
`output_invalid`.

`dir(Artifact)` returns the Artifact and a writable directory.
`upload(Artifact, path)` copies a file or a directory. `open(artifact)`
returns the granted read-only directory. Forward an Artifact by returning
it in a declared Artifact field.
Declare Artifact fields with `include "tsugumi/artifact.fbs";`. The
host seals created directories when the call returns.

## Julia values

Value mapping follows the FlatBuffers object API. Missing nested tables and
optional scalars use `nothing`. Absent string and vector fields use their
object API defaults. Enum names are strings, with unknown enum values represented
as integers. The interpreter parses integer tokens as `Int128` to keep the full
signed and unsigned 64-bit range. Unions use `(; type="Variant", value=payload)`
or a dictionary with those keys. A union without a selected member uses
`nothing`. The strings `"NaN"`, `"Infinity"`, and `"-Infinity"` represent
nonfinite floats. You can also return Julia's nonfinite float values.
A `[ubyte]` field uses standard padded base64, with `base64encode` and
`base64decode` available from Julia's `Base64` standard library.

`send(view, bytes)` takes a declared view name and an
`AbstractVector{UInt8}`. `send(value)` supports strings, real scalars,
`Markdown`, `Scalar`, `Table`, `Image`, `Points`, `LineChart`,
`Plotly`, and `VegaLite`. Local argument and encoding errors throw before
calling the host. If the host cannot accept the message, `send` throws
`ErrorException("display was rejected")`.

The SDK bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `text` view displays values sent with `send("text")`.
See the [view protocol](cpp-sdk.md#display-a-value).

`recv(view, timeout_ms)` returns `(status, bytes)` with status
`:message`, `:timeout` or `:cancelled`. A negative timeout waits without
a deadline. The SDK frames sends in messages of at most 16 MiB.
Tasks on a separate Julia thread can send and receive after `main` returns.
The embedder starts two Julia threads when `JULIA_NUM_THREADS` is unset. Outside a call, `cancelled()` returns true and
`remaining_ms()` returns 0. Throw `CardFailure(:kind, "message"; cell="name")` to
set the failure kind and optional cell. Returning a `CardFailure` reports the
same failure without a source location. For exceptions, the SDK reports a
Card-definition-relative filename and one-based line when available. Set
`sourcePrefix` to the location of `src` relative to the Card definition if it
differs from `"src"`. Use `""` when `src` is the Card definition directory.

The first valid call initializes Julia and loads the Card for the life of the
worker. The SDK loads its pinned JSON.jl environment from the Nix closure and
uses `HOME/.julia` for mutable cache data.

The builder accepts `tools` for runtime executables. See [Runtime tools](../README.md#runtime-tools).

Build `checks.<system>.juliaAbi` to run the Julia Card through the ABI harness.
