# Card SDK contract

This kit implements the unreleased Card ABI, version 0. Each language has a
`sdk/<lang>/default.nix` entry taking `{ pkgs }`. It exports its
`<lang>Card` and `<lang>Api` builders and `views`.

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
Tsugumi generates this file for each Code Card. It contains an empty
`Inputs` table when the Card has no connected inputs.

Pass the evaluated `params` attrset. Each value is a bool, signed 64-bit
integer, double, string, or non-empty homogeneous list of one of those types.
Use the types to generate declarations and embed values in `run` as a typed
constant. The check must assert that changing a value with the same type keeps
the `api.drvPath` stable and changes `run.drvPath`. The library lives at
`lib/main.so`. Export the single entry point `tsugumi_main`.
A definition can return Nix exports and can build variants by returning
`run = [ { label = "…"; run = …; } ];`.

## Native calls and shared processes

The ABI lends the `CardInput` envelope for the duration of the call. A
returned result is a fresh libc `malloc` allocation. The host frees the
base pointer after its last reader, including when the native entry returns
a nonzero status. The host keeps loaded Card images alive while background
effects or managed code can reference them. Calls into a given Card image
are serial.

| SDK | Input access and process limits |
| --- | --- |
| C | Use generated FlatBuffers readers on the borrowed input and payloads. Allocate the serialized result with libc `malloc`. |
| C++ | Generated inputs borrow verified tables. The worker supplies aligned envelopes and payloads. For unaligned buffers from other hosts the SDK uses aligned scratch storage. `UnPack()` explicitly materializes an owned table. |
| Rust | Generated inputs borrow verified slices and tables. The FFI copies the finished builder bytes to a libc allocation for transfer. |
| Go | The adapter borrows the envelope. It copies each selected payload before unpacking because generated Go values can retain slices into it. Each process can load one Go c-shared Card library. |
| Python | C++ verifies borrowed payloads. The Python object API needs owned bytes because arrays may outlive the call. Cards share CPython, while SDK modules, generated bindings, parameters and Card source modules have private names. Ordinary imports of Card-local modules resolve within that Card. Use relative names for dynamic imports of local code. Third-party packages and extension state are shared, so Cards need compatible packages and the same libpython. |
| JavaScript | Each Card has a Node environment and V8 isolate on a shared Node platform. V8 gets an owned copy because JavaScript typed arrays are mutable and can outlive the call. The host receives the serialized SDK result without rebuilding the envelope. |
| JVM | Cards share one JVM and each gets a class loader with its own application classes, SDK, dependencies, parameters and JNI bindings. JNI supplies a read-only direct buffer to the FlatBuffers decoder, which materializes Java port objects. The final Java byte array is copied to the host allocation. |
| .NET | hostfxr shares one CoreCLR runtime and loads each Card assembly in its own load context. The managed FlatBuffers package uses managed byte arrays, including when unpacking port payloads. The host receives the serialized managed result without rebuilding the envelope. |
| Ruby | MRI starts once on its owner thread. Each Card gets a private SDK module and entry class with its own parameters and native effects. The input is converted from verified FlatBuffers to JSON for Ruby. RubyGems, loaded libraries and globals are shared and dependencies must be compatible. |
| Julia | One Julia runtime owns calls on a dedicated thread. Each Card has a private module, bindings and effects. The input is converted from verified FlatBuffers to JSON for Julia. The Julia project, depot and loaded packages are process-wide. |
| R | One R runtime owns calls on a dedicated thread and each Card has a private environment with local `.Call` bindings. The input is converted from verified FlatBuffers to JSON for R. Cards must agree on the library paths and font configuration. An extension with a process-global cache of `R_GetCCallable("tsugumi", …)` pointers must resolve them per Card or use one Card per process. |

The shared-runtime SDKs target Cards built against the same runtime closure.
A pack must group compatible closures before sharing a process. Card module
namespaces do not isolate process state such as working directories or
environment variables.

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
returning the read-only directory. Copy a file into the writable
directory using its basename. Call `artifact_create` and `artifact_open`
during a Card call.

The host creates each Artifact under a new random 128-bit ID and seals its
directory when the call returns. Knowing an ID is enough to open it:
`artifact_open` works for any sealed Artifact of the project. Forwarding an
Artifact means writing its ID in an output; the SDK does not scan payloads
for IDs.

`dir` and `upload` take a keep option, true by default, in the language's
idiomatic form. Keep passes `TSUGUMI_ARTIFACT_KEEP` to `artifact_create`;
without it the Artifact exists only while its run runs, later Cards of the
same run can open it, and it is removed with its record when the run ends.
There is no Card or project setting for keep. When `artifact_open` fails,
raise the language's error with a message naming the ID: `artifact <uuid>
is not available: it was not kept after its run, was deleted, or is not in
this project`, where `<uuid>` is the lowercase 8-4-4-4-12 hex form.

A Card lets the person running choose keep per run by reading it from a
parameter:

```nix
# params.nix
{
  keep_scratch = false;
}
```

```python
scratch, directory = Artifact.dir(keep=params.keep_scratch)
```

## Display and effects

Return `views = [ { name; type = "html" | "dawn"; blob; } ]` at the
definition's top level. Use a self-contained HTML file for HTML and a Wasm
binary for Dawn. Use the vendored `abi/tsugumi_view.h` for the Dawn view
ABI, callback declarations and pinned WebGPU imports.

Call `view_open` with a declared name to open a view and receive a session
and an opener route. The initial presentation can be a panel, a window or
fullscreen. Window sizing uses logical pixels. Specify both dimensions or
use `0, 0` to let the UI choose. Panel and fullscreen require `0, 0`.
Unknown names and modes, and invalid dimension pairs, are errors.
The public argument order is `name, mode, flags, width, height`.
Flags form a 32-bit unsigned bitmask. Pass zero for the default, bit 0
(`HIDE_CURSOR`) to hide the cursor or bit 1 (`LOCK_POINTER`) to request
pointer lock. Both bits can be set together. While locked, Dawn pointer
callbacks receive relative movement in `x` and `y`. The user can release
pointer lock. A Card running without a UI still receives a session and
route. It can close that session itself, and its sends are dropped.

A session identifies one open view. `send` addresses that session and
carries up to `TSUGUMI_MESSAGE_MAX` bytes (16 MiB) per ABI message.
Send waits cancellably if the next hop lacks room for a maximum-size message.
A route identifies one Card process's mailbox for that session. A Card that
receives a session ID can send to it and call `route_register` to create its
own route. Both IDs are 16-byte capabilities scoped to the project. Share
them only with recipients that should be able to use them.

`recv` waits on all routes in the process and reports a message or close
event with its route and session, a timeout, or cancellation. A negative
timeout waits without a deadline. Copy borrowed ABI bytes before the next
receive on the same thread. The view effects are available between calls.
Routes last until `route_unregister`, process exit or window close. A view
stays open when its last route goes away. The user or any Card that holds the
session can close it. Closing queues one close event for each live route and
drops later sends to the session. Closing an already closed session succeeds
without another event. Messages to dead routes are dropped.

| SDK | Close a window | Registering after close |
| --- | --- | --- |
| C ABI | `view_close(host, &session)` | `TSUGUMI_VIEW_WINDOW_CLOSED` (−2) |
| C++ | `card::display::view_close(session)` | `card::display::WindowClosed` |
| C# | `Effects.ViewClose(session)` | `Effects.WindowClosedException` |
| Go | `ViewClose(session)` | `ErrWindowClosed` |
| Java | `Display.viewClose(session)` | `Display.WindowClosed` |
| JavaScript | `viewClose(session)` | Error with `code === "window_closed"` |
| Julia | `view_close(session)` | `WindowClosed` |
| Python | `tsugumi.view_close(session)` | `tsugumi.WindowClosed` |
| R | `view_close(session)` | Condition of class `tsugumi_window_closed` |
| Ruby | `Tsugumi.view_close(session)` | `Tsugumi::WindowClosed` |
| Rust | `view_close(&session)` | `FailureKind::not_found` from `route_register` |

View traffic is live and stays out of run history. Use outputs or logs to keep
results.

### HTML view API

```js
tsugumi.onmessage = bytes => {
  // bytes is a Uint8Array containing one Card ABI message.
  handleMessage(bytes)
}
await tsugumi.send(replyBytes, "screens/settings")
await tsugumi.send(replyBytes, "*")  // All Cards registered on this window.
```

The bundled HTML views expect SDK frames. The 20-byte header has the
little-endian `uint32` magic `0x4D475354`, an 8-byte transfer ID, and a
little-endian `uint32` chunk index and count. Each chunk carries up to
`TSUGUMI_MESSAGE_MAX - 20` payload bytes. An empty payload uses one frame
with just the header. A multipart value uses a random transfer ID and
sends its chunks in index order. The view assembles interleaved values by
transfer ID and keeps up to 16 incomplete transfers.

### Dawn view API

Dawn receives each Card ABI message as sent. In C++, use
`card::display::send_raw(session, bytes)` for one unframed message.
Read incoming data with `tsugumi_view_read` inside `tsugumi_view_message`.
Reply with `tsugumi_view_send(to, data, size)`. Use a NUL-terminated
project-root Card path or `"*"`. Each registered live process of the
addressed Cards receives the message. An unknown or unregistered
address returns an error. Route IDs are private to the Card ABI.

Build C view sources with `sdk.dawnView { inherit name src; }` from the
C++ kit. See the [Dawn SDK guide](dawn-sdk.md) for the clock, optional
callbacks, input helpers and embedded font atlas.

## Check every SDK

Use `nix/contract-check.nix { inherit pkgs; }` with the language's `name`,
fixture `card`, `api` and shared `runtime`. Implement the fixture behavior
in that language following `tests/card/src/main.cc` or
`tests/python/src/main.py`. It covers both Merge cases, Collect with a
member, a direct Artifact, nested Artifact listing in a table, vector and
union, forwarding, the keep flags of `dir`, `upload` and `dir` with keep
false, an unknown Artifact ID, required and optional outputs, malformed
inputs, opening sessions with combined flags and with zero flags,
registering and unregistering a route, raw bytes
per message, receive statuses, and close events with their session and route
IDs. It also exercises send and receive after the entry point returns.
`tests/view-reassembly.mjs` exercises HTML framing independently.
The two fixture schema files and params are in `tests/contract/`.
Use the input and API checks alongside the
language check and enable the language in `nix/pending-sdk-checks.nix`.

```sh
"$SP/heavy.sh" nix build --no-link -L .#checks.aarch64-darwin.cppAbi
"$SP/heavy.sh" nix build --no-link -L .#checks.aarch64-darwin.pythonAbi
"$SP/heavy.sh" nix build --no-link -L .#checks.aarch64-darwin.inputCard
```
