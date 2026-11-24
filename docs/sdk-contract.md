# Card SDK contract

This kit implements the unreleased Card ABI, version 0. Each language has a
`sdk/<lang>/default.nix` entry taking `{ pkgs }`. It exports its
`<lang>Card` and `<lang>Api` builders, `views`, and the shared
`input` and `artifact` functions from `sdk/input`.

## Card definition

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/cpp> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./_inputs.fbs;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.cppCard (common // { src = ./src; });
  api = sdk.cppApi common;
  views = sdk.views;
}
```

In the definition function, `inputs` holds upstream Nix exports keyed by
Card ID. The builder's `inputs` argument is the generated schema path.
Tsugumi generates this file for each Code and Input Card. It contains an empty
`Inputs` table when the Card has no connected inputs.

Pass the evaluated `params` attrset. Each value is a bool, signed 64-bit
integer, double, string, or non-empty homogeneous list of one of those types.
Use the types to generate declarations and embed values in `run` as a typed
constant. The check must assert that changing a value with the same type keeps
the `api.drvPath` stable and changes `run.drvPath`. The library lives at
`lib/main.so`. Export the single entry point `tsugumi_main`.
A definition can return Nix exports and can build variants by returning
`run = [ { label = "…"; run = …; } ];`.

## Shared Nix and codegen

Import `nix/sdk-dependencies.nix { inherit pkgs; }` in each SDK.

| Function or dependency | Contract |
| --- | --- |
| `cardInterface { params, inputs, outputs }` | Produces `_inputs.fbs`, `_outputs.fbs`, their includes, ABI schemas under `tsugumi/` and `param-types.json`. The param type strings are `bool`, `long`, `double`, `string` and bracketed lists. |
| `paramValues params` | Writes the evaluated values to a JSON build input for the language's embedding step. Reference it from the run builder. |
| `portCodegen` | Provides the Python module `tsugumi_ports`. Call `load_card_schemas(interface, reflection_fbs)` with the pinned FlatBuffers reflection schema. |
| `abiHeaders` | Installs the C ABI and C++ protocol headers. The `runtime` derivation additionally provides the C++ adapter library. |
| `publishApi "language" script` | Runs a copy/install script with `$api` pointing to `$out/share/tsugumi/api`. Copy the declarations and a README with Inputs, Outputs and Params sections. |
| `views.common` | Bundled HTML views including Text, Markdown, Table, Image, LineChart and Points. `views.plotlyViews` adds Plotly. |

`load_card_schemas` returns ordered `inputs`, `outputs` and `params`.
Each port has `name`, `type` and `required`. An input also has
`candidates`, ordered `(schema, type)` records. Select a candidate using
`CardPort.schema` before verifying and decoding the payload. An unknown
schema produces `input_invalid`. Represent multiple candidates as a
language-level sum type. The bytes are the selected table's FlatBuffer.
A Collect port decodes as `tsugumi.builtin.Collected` and carries opaque
`Envelope` items. `collected_members` provides the reflected candidate
schema IDs and types for those items. For the two shared ABI roots whose
declarations are supplied by the ABI, codegen knows their fixed core IDs.

Pack each output with the declared table as its root. A missing `(required)`
output produces `output_invalid`. An omitted optional field leaves that port
out of `CardResult.outputs`. The worker stamps output schema IDs.

## Artifacts

Use `include "tsugumi/artifact.fbs";` in user schemas and expose a native
Artifact type wherever the schema names `tsugumi.Artifact`. Provide
`dir()` returning an Artifact and its writable directory, `upload(path)`
returning the Artifact after copying the file or directory, and `open()`
returning the granted read-only directory. Copy a file into the writable
directory using its basename. Call `artifact_create` and `artifact_open`
during a Card call.

For every output, visit each nested table, vector and active union member.
List each distinct Artifact ID in `CardPort.artifacts`, including IDs
forwarded from granted inputs. The host grants IDs from input sidecars and
seals directories after the call.

An Input Card's definition returns the result of the shared helper as
`run`. Its `values` attribute set maps output port names to flatc JSON
table values. A nested `sdk.artifact ./data.csv` marks a file or folder for
copying into the immutable build output.

```nix
run = sdk.input {
  outputs = ./_outputs.fbs;
  values.example = { label = params.label; file = sdk.artifact ./data.csv; };
};
```

The build produces `share/tsugumi/input/ports/<port>`,
`ports/<port>.artifacts` with one lowercase hex ID per line, and
`artifacts/<id>/` under that same Input root. IDs are the first 16 bytes of
SHA-256 over the UTF-8 port name, one NUL byte and the source store path.
The build computes this byte sequence directly because Nix evaluation
strings cannot represent NUL. Core checks missing required Input Card
ports when it emits the build output.

## Display and effects

Return `views = [ { name; type = "html" | "dawn"; blob; } ]` at the
definition's top level. Use a self-contained HTML file for HTML and a Wasm
binary for Dawn. Use the vendored `abi/tsugumi_view.h` for the Dawn view
ABI, callback declarations and pinned WebGPU imports.

`display.send(view, bytes)` emits chunks with a 16-byte little-endian header
of four unsigned 32-bit integers. In order, write magic `0x4D475354`,
a sequence per view per process, the zero-based chunk index, and the chunk
count. Each payload is at most `TSUGUMI_MESSAGE_MAX - 16` bytes. Send an
empty payload as one header and serialize chunks for each view. The bundled
HTML views reassemble the frames received via their MessagePort before
rendering the data. A Dawn view receives each Card ABI message directly.
In C++, use `card::display::send_raw(view, bytes)` for one unframed message.

`display.recv(view, timeout)` returns a message, timeout, or cancellation.
The timeout is in milliseconds and a negative value waits without a deadline.
Copy received bytes before the next receive on that thread. Send and receive
are process-lifetime effects and can run on Card threads between calls.
The other effects are scoped to the active call. Keep the effects table order
in `abi/card_abi.h`.

## Check every SDK

Use `nix/contract-check.nix { inherit pkgs; }` with the language's `name`,
fixture `card`, `api` and shared `runtime`. Implement the fixture behavior
in that language following `tests/card/src/main.cc` or
`tests/python/src/main.py`. It covers both Merge cases, Collect with a
member, a direct Artifact, nested Artifact listing in a table, vector and
union, forwarding, grant refusal, required and optional outputs, malformed
inputs, send framing, and receive statuses. It also exercises send and
receive after the entry point returns. The two fixture schema files and
params are in `tests/contract/`. Use the input and API checks alongside the
language check and enable the language in `nix/pending-sdk-checks.nix`.

```sh
"$SP/heavy.sh" nix build --no-link -L .#checks.aarch64-darwin.cppAbi
"$SP/heavy.sh" nix build --no-link -L .#checks.aarch64-darwin.pythonAbi
"$SP/heavy.sh" nix build --no-link -L .#checks.aarch64-darwin.inputCard
```
