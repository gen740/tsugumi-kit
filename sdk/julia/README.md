# Julia Cards

Use `kit.lib.${system}.juliaCard { src = ./src; inherit contract; }` in a Nix
Card definition. The derivation installs `lib/main.so`. The contract directory
contains `_contract.json` and the core's `_inputs.fbs`, `_outputs.fbs` and
`_params.fbs`. Put the Card in `src/main.jl`, or set `entry` to another
source-relative filename.

Define `main(input)` and return a `NamedTuple` or a dictionary keyed by output
port name. Input ports support `input.port` and `input["port"]`. Read parameters
with `input.params["parameter"]`. Each table is a dictionary keyed by its schema
field names. Omit optional output ports or set them to `nothing`.

For example, a File input named `source` and a File output named `copy` can use:

```julia
function main(input)
    cancelled() && throw(CardFailure(:cancelled, "Stopped"))
    file = "copy.txt"
    cp(path(input, "source"), file; force=true)
    display("Created " * file; record=true)
    return (; copy=(; value=file))
end
```

Create new File and Directory outputs under the working directory. Finish
writing and close them before returning. Return the input path unchanged to
forward an input. The worker validates and snapshots the outputs.

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

`display(value; record=false)` accepts strings, real scalars, and the SDK types
`Markdown`, `Scalar`, `Table`, `Image`, `Points`, `LineChart`, `Plotly`,
`VegaLite`, and `View`. Tasks scheduled on a separate Julia thread can keep
sending displays after `main` returns. Outside a call, `cancelled()` returns true and
`remaining_ms()` returns 0. Throw `CardFailure(:kind, "message"; cell="name")` to
set the failure kind and optional cell. The SDK reports a source-relative
filename and a one-based line from Card exceptions when available.

The first valid call initializes Julia and loads the Card for the life of the
worker. The SDK loads its pinned JSON.jl environment from the Nix closure and
uses `HOME/.julia` for mutable cache data.
