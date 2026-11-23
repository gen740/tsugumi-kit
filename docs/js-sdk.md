# Writing a JavaScript Card

This guide targets **the Card ABI (unreleased)**. A JavaScript Card exports one
function from `src/main.mjs`. It receives typed input ports and parameters and
returns an object keyed by output port. The function can return a Promise.

```js
import { writeFile } from "node:fs/promises";
import { Failure, FailureKind, Text } from "@tsugumi/card";

/** @type {import("@tsugumi/card").Card} */
export default async function run({ params }, { cancelled, display }) {
  if (cancelled()) throw new Failure(FailureKind.cancelled, "Stopped before writing");
  const file = "note.txt";
  const bytes = new TextEncoder().encode(params.text ?? "");
  await writeFile(file, bytes);
  display.send(new Text(`Wrote ${bytes.length} bytes to ${file}`));
  return { file };
}
```

The [note template](../templates/js-write-note/) includes the matching Card
definition. Declare input ports in `_card.json`, output types in `_outputs.fbs`, and
parameter defaults in `params.nix`. Select the SDK in `default.nix`.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/js> { inherit pkgs; };
  common = { inherit inputs; outputs = ./_outputs.fbs; params = ./params.nix; };
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

The builder generates `@tsugumi/card` from the supplied schemas. The builder's
`bindings` passthru contains the generated package under `js/`.
`CardInput.params` is the generated `ParamsT` object.
`input.ports[name]` returns the value or `undefined`. The generated
`CardPorts` type lists the connected ports. Use `input.need(name)` to get a
needed value of type `T`. If the port is absent, including when it is
disconnected, `need` throws a `Failure` with kind `input_missing` and a
message naming the port.

Use the generated type under `outputs` to construct a FlatBuffer output.
For example, `outputs.weather.ReadingT` uses the authored `weather.Reading` table. The generated
namespaces expose child tables, structs, and enums. For mixed-type Merge
ports, the generated input and output types are unions of the member object
types. Narrow an input with `instanceof`, and return an instance of the
corresponding output member type. The SDK handles verification and boxing.
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
When the worker process exits, the SDK stops the Node environment and
waits for it, including its `worker_threads`, before libnode's exit-time
destructors run. A `worker_threads` Worker blocked in a synchronous call
delays that exit, as in Node.

Throw a `Failure(FailureKind.cancelled, message)` for a structured failure.
An ordinary exception or rejected Promise becomes `worker_failed`.
When Node supplies a Card source location, the SDK includes the relative
file path and line.

## Files and worker lifetime

Declare a Path output as `string (tsugumi: "path")` in `_outputs.fbs` and
return its path as a string. Create the file or directory under the current
working directory and complete its writes before returning. A Path input
is a staged absolute path string. To forward it, return that string unchanged. See
[Files, directories and worker lifetime](cpp-sdk.md#files-directories-and-worker-lifetime)
for the worker's filesystem rules.

The Card library links a shared Node 24 build and creates one
`node::CommonEnvironmentSetup` in the worker process. With `reuseWorker`,
module state and loaded dependencies stay warm for subsequent calls of
the same Card. Code running between calls can publish displays, but
should finish all work needed for output files before its invocation returns.
