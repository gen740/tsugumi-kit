# Writing a JavaScript Card

This guide targets **Card ABI 9 (unreleased)**. A JavaScript Card exports one
function from `src/main.mjs`. It receives typed input ports and parameters and
returns an object keyed by output port. The function can return a Promise.

```js
import { writeFile } from "node:fs/promises";
import { Failure, FailureKind, Markdown, outputs } from "@tsugumi/card";

/** @type {import("@tsugumi/card").Card} */
export default async function run({ params }, { cancelled, display }) {
  if (cancelled()) throw new Failure(FailureKind.cancelled, "Stopped before writing");
  await writeFile("note.txt", params.text ?? "", "utf8");
  display.send(new Markdown("Wrote **note.txt**"));
  return { file: new outputs.file.FileT("note.txt") };
}
```

The [note template](../templates/js-write-note/) includes the matching Card
definition. Declare inputs and parameters in `_card.json`, output types in
`_outputs.fbs`, and select the SDK in `default.nix`.

```nix
{ inputs, ... }:
let
  sdk = inputs.tsugumi.jsSdk;
in
{
  run = sdk.jsCard { src = ./src; };
  views = sdk.views;
}
```

`jsCard` produces `lib/main.so`. The core supplies its contract through
`inputs.tsugumi.jsSdk`. For standalone builds, use
`kit.lib.${system}.jsSdk.jsCard { inherit src contract; }` with the directory
containing the generated `_inputs.fbs`, `_outputs.fbs`, `_params.fbs`, and
`_contract.json`.

## Types and dependencies

The builder generates `@tsugumi/card` and its TypeScript declarations from the
contract. The builder's `bindings` passthru contains the generated package
under `js/`. `CardInput.ports` uses the exact port names, and absent optional
ports are `undefined`. `CardInput.params` is the generated `ParamsT` object.
Use `outputs.<port>.<Table>T` to construct output values. The generated
namespaces also expose child tables, structs, and enums. For example,
`outputs.file.FileT` is the File table for the `file` output above.
Omit optional outputs or set them to `undefined`. Return `{}` from a Card
with no outputs. FlatBuffers 64-bit integers are JavaScript `bigint` values.

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

Call `ctx.display.send(value)` to publish a value. For custom views, declare
the view alongside `sdk.views` and call `ctx.send(viewName, bytes)` with
opaque `Uint8Array` data. Both return the host status, where zero indicates
acceptance and a nonzero value means the host could not accept the message.
The host forwards view names to the UI, which reports undeclared views.
Argument and encoding errors throw JavaScript exceptions before the SDK
calls the host. These functions can also be imported from `@tsugumi/card`.
The [bundled HTML views](cpp-sdk.md#display-a-value) define their private encodings.

Use `ctx.cancelled()` to poll for cancellation and `ctx.remainingMs()` to
read the deadline in milliseconds. `-1` means the active call has no
deadline. Between calls, they return `true` and `0`, respectively. The
Node event loop keeps running between calls, and callbacks on that loop
and Node `worker_threads` can use `send` for the worker's lifetime.

Throw a `Failure(FailureKind.cancelled, message)` for a structured failure.
An ordinary exception or rejected Promise becomes `worker_failed`.
When Node supplies a Card source location, the SDK includes the relative
file path and line.

## Files and worker lifetime

Create new File or Directory outputs under the current working directory.
Complete their writes before returning, and set each output's `value` to
its path. For an input File or Directory, read its absolute `value` path
during the active call. To forward it, return that path unchanged. See
[Files, directories and worker lifetime](cpp-sdk.md#files-directories-and-worker-lifetime)
for the worker's filesystem rules.

The Card library links a shared Node 24 build and creates one
`node::CommonEnvironmentSetup` in the worker process. With `reuseWorker`,
module state and loaded dependencies stay warm for subsequent calls of
the same Card. Code running between calls can publish displays, but
should finish all work needed for output files before its invocation returns.
