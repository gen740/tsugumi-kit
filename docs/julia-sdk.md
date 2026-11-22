# Julia SDK for Card ABI 9, revision 2

Put `main(input)` in `src/main.jl`. The SDK embeds Julia in the Card's
`lib/main.so`.

```nix
{ pkgs, params, inputs, schemas }:
let
  sdk = inputs.tsugumi.juliaSdk;
in
{
  run = sdk.juliaCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
```

The builder uses the current `_inputs.fbs`, `_outputs.fbs`, and
`_params.fbs` in `schemas`. The Card editor's SDK section and the build
output's `share/tsugumi/api/` expose `card.jl`, the schemas, and
a README describing the current port and parameter types. Set `entry` to
choose another file relative to `src`.

```julia
function main(input::Inputs)
    cancelled() && throw(CardFailure(:cancelled, "Stopped before writing"))
    file = "note.txt"
    open(io -> println(io, input.text["value"]), file, "w")
    return (; file)
end
```

The [Julia starter](../templates/julia-write-text/) uses this entry with a
FlatBuffer `text` input and a Path `file` output.

## Inputs and outputs

Read a needed input with `input.text`, `input[:text]`, or `input["text"]`.
When it is absent, including when the port is disconnected, the SDK reports
`input_missing` and names the port. Use `get(input, :text, nothing)` to read
an optional value of type `Union{T, Nothing}`. Read parameters through
`input.params["parameter"]`. A port named `params` is read through indexing
or `get(input, :params, nothing)`.

Return a `NamedTuple` or dictionary keyed by output port name. Omit an
optional output or set it to `nothing`. A missing required output produces
`output_invalid`. Paths are strings. Finish writing and close new files or
directories under the call's working directory before returning their paths.
Return the input path unchanged to forward an input.

For a mixed Merge, the adapter verifies the nested buffer and decodes the
selected member table. Read the member name at `arrival["type"]` and its
dictionary at `arrival["value"]`. To return a member on a Merge output, use
a dictionary with those keys or `(; type="Temperature", value=table)`.
The generated README lists the member names for each port.

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

The SDK at `lib.${system}.juliaSdk` bundles the `line-chart` and `points` HTML views.
Declare the matching views in your Card's `default.nix` with
`views = sdk.views ++ [ ... ];`.
The bundled `text` view displays values sent with `send("text")`.
See the [view protocol](cpp-sdk.md#display-a-value).

Tasks scheduled on a separate Julia thread can keep
sending displays after `main` returns. Outside a call, `cancelled()` returns true and
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

Build `checks.<system>.juliaAbi9` to run the Julia Card through the ABI harness.
