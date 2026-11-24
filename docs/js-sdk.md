# Writing a JavaScript Card

This guide targets **the Card ABI (unreleased)**. A JavaScript Card exports one
function from `src/main.mjs`. It receives typed input ports and a context and
returns an object keyed by output port. The function can return a Promise.

```js
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
import { Artifact, Failure, FailureKind, Text, params } from "@tsugumi/card";

/** @type {import("@tsugumi/card").Card} */
export default async function run(_input, { cancelled, display }) {
  if (cancelled()) throw new Failure(FailureKind.cancelled, "Stopped before writing");
  const [file, directory] = Artifact.dir({ keep: params.keep_output });
  const bytes = new TextEncoder().encode(params.text);
  await writeFile(join(directory, "note.txt"), bytes);
  display.send(new Text(`Wrote ${bytes.length} bytes to note.txt`));
  return { file };
}
```

The [note template](../templates/js-write-note/) includes the matching Card
definition. Declare input ports in `_card.json`, output types in `_outputs.fbs`, and
current parameter values in `params.nix` (omit the file when the Card has no
params). Select the SDK in `default.nix`.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/js> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./_inputs.fbs;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.jsCard (common // { src = ./src; });
  api = sdk.jsApi common;
  views = sdk.views;
}
```

`jsApi` publishes the generated TypeScript sources, `index.d.ts`, and a typed
README under `share/tsugumi/api/`. The run output contains `lib/main.so`.

## Types and dependencies

The builder generates `@tsugumi/card` from the supplied schemas. Import
`params` from that package to read the frozen build-time values. The
generated `Params` type records their types. FlatBuffers 64-bit integers
and integer params are JavaScript `bigint` values.

Input ports appear under `input.ports`. Use `input.need(name)` when the
Card requires the port. The generated schema contains only connected ports.
The SDK verifies each arrival against the candidate selected by schema ID.
A mixed Merge yields one of the member object types. Use `instanceof`
with the generated class from `inputs` to narrow it. Collect yields opaque
envelopes containing a schema ID and payload bytes. Use
the generated member accessor with `ByteBuffer` to decode a recognized
envelope.

Return an instance of the generated type under `outputs` for each port.
For example, `outputs.weather.ReadingT` represents `weather.Reading`.
Omit optional outputs or set them to `undefined`. A missing required output
produces `output_invalid`.

For npm dependencies, put `package.json` and `package-lock.json` in `src`.
List packages needed at runtime under `dependencies`. Nix imports the lock,
installs production dependencies, and includes them in the Card derivation.
The embedded Node loader resolves them locally at runtime. Use
`nativeBuildInputs` and `buildInputs` for build dependencies of native npm
addons. The builder uses Node 24 for both npm and embedding.
The generated SDK package is supplied by the builder.

Use `tools = [ pkgs.coreutils ];` to add external executables and invoke them
with argument arrays, for example using `execFile` from `node:child_process`.
The SDK sets `PATH` to the Card's `bin` directory before Node starts when
`tools` is nonempty. An empty list leaves the worker's `PATH` unset.
See [Runtime tools](../README.md#runtime-tools) for `PATH` and shell behavior.

## Displays and effects

| Value | View | Payload |
| --- | --- | --- |
| String or `new Text(text)` | `text` | Plain text |
| `new Markdown(text)` | `markdown` | Markdown |
| `new Table(columns, rows)` | `table` | Column names and rows with matching widths |
| `new Image(bytes, mediaType?, alt?)` | `image` | Encoded image as a `Uint8Array` |
| `new LineChart(series)` | `line-chart` | Series with `id`, `label` and equal-length finite `x` and `y` arrays |
| `new Points(x, y, z, colors?)` | `points` | Equal-length position arrays and optional packed `0xRRGGBB` colors |

Call `ctx.display.send(value)` to open a bundled view as a panel. Pass a
session as the second argument to update an existing bundled view. For
custom views, declare the view alongside `sdk.views` and use
`ctx.viewOpen(name)` to obtain its route and session. Call `ctx.send(session, bytes)` with
opaque `Uint8Array` data. Both return the host status, where zero indicates
acceptance and a nonzero value means the host could not accept the message.
Use `ctx.routeRegister(session)` to create a route and
`ctx.routeUnregister(route)` to release it.
Argument and encoding errors throw JavaScript exceptions before the SDK
calls the host. These functions can also be imported from `@tsugumi/card`.
The [bundled HTML views](sdk-contract.md#html-view-api) use SDK-framed typed values.

Use `ctx.recv(timeoutMs)` to receive an object with `bytes` and a `status`
of `"message"`, `"close"`, `"timeout"` or `"cancelled"`. Message and close
events also carry `route` and `session`. A negative timeout waits without
a deadline.
Send and receive work on Node worker threads after the Card returns.
`ctx.cancelled()` and `ctx.remainingMs()` report the active call's state.
Throw `Failure` to report a structured Card failure.

## Artifacts and worker lifetime

Declare an Artifact field using `include "tsugumi/artifact.fbs";`.
`Artifact.dir()` returns an Artifact and a writable directory.
`Artifact.upload(path)` copies a file or directory into a new Artifact.
Both keep the Artifact after its run by default. Pass `{ keep: false }` for
one that exists only while its run runs: later Cards of the same run can
open it, and it is removed with its record when the run ends. To let the
person running choose per run, read keep from a param:

```nix
# params.nix
{ keep_scratch = false; }
```

```js
const [scratch, directory] = Artifact.dir({ keep: params.keep_scratch });
```

Call `artifact.open()` to get the read-only directory of any sealed Artifact
of the project whose ID the Card knows. Opening fails with a message naming
the ID when it was not kept after its run, was deleted, or is not in this
project. Return the same Artifact to forward it. Finish writing before
returning so the host can seal the directory.

The Card embeds Node 24 and keeps its module state and dependencies warm
when the worker is reused.

Build `checks.<system>.jsAbi` to run the ABI harness.
