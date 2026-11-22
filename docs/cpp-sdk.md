# Writing a C++ Card

This guide targets **Card ABI 9 (unreleased)**. The released worker cannot run these Cards yet.

## Return a value from `run`

A Card has the shape `(input) -> output`. Include the generated `card.hh` and define one free function. Here is the complete source for a Card with a required input port `celsius` and a required output port `fahrenheit`, both tables with a `value:double` field.

```cpp
#include "card.hh"

auto run(const Card::Inputs& input) -> Card::Output {
  const double fahrenheit = input.celsius.value * 1.8 + 32.0;
  card::display::send(card::display::Scalar{fahrenheit, "°F"});
  Card::Output output;
  output.fahrenheit.emplace().value = fahrenheit;
  return output;
}
```

For an input of 20, the next Card receives 68 on the output port. The value also appears live, as markdown. Returning the output and displaying a value are independent operations. The [temperature conversion template](../templates/temperature-conversion/) includes the input Card, both output schemas and the wire between them.

| Card definition file | What to put there |
| --- | --- |
| `_card.json` | The Card type, input port names and parameter declarations. Connected producers supply the input types. |
| `_outputs.fbs` | The FlatBuffers root table `Outputs`. Each field names an output port and its table type. Mark required ports with `(required)`. |
| `src/*.cc` | The Card implementation, including `run`. |
| `default.nix` | Select the injected C++ SDK, expose its build result as `run` and forward its `views`. The run derivation contains `lib/main.so`. |

The core prepares `_inputs.fbs`, `_outputs.fbs`, `_params.fbs` and `_contract.json` for the SDK. The SDK uses `flatc --cpp --gen-object-api` and its own generator to produce the headers and the `tsugumi_main` adapter. Read the generated `card.hh` to see the exact member names and types for your Card.

- A required input is a native FlatBuffers object (for example, a table named `Temperature` has an object type ending in `TemperatureT`). An optional input is a `std::optional` of that type. Test it before dereferencing it.
- `input.params` is the generated `Card::Params` object, an alias for `parameters::ParamsT`. A parameter named `threshold` is available as `input.params.threshold`.
- Every member of `Card::Output` is a `std::optional` of its port's object type. Use `emplace()` to produce a port and fill its required fields. Leave an optional port disengaged to omit it. Missing required ports or children, null table elements in vectors, and union tags without values fail the call with `output_invalid`.
- The C++ generator renames reserved schema identifiers as needed. The port names on the wire stay as declared in the contract.

Use the SDK in the Card's `default.nix` with the contract and Card definition directory bound to its builder.

```nix
{ inputs, ... }:
let
  sdk = inputs.tsugumi.cppSdk;
in
{
  run = sdk.cppCard { src = ./src; };
  views = sdk.views;
}
```

The builder also accepts optional `buildInputs` and `cxxflags` lists. SDK selection belongs in `default.nix`. Omit `language` from `_card.json`.

The kit exports `tsugumi-kit.lib.${system}.cppSdk`. To bind the builder while retaining the SDK views, use `cppSdk // { cppCard = cppSdk.cppCard { inherit contract definitionDir; }; }`. Here `contract` is the directory with the four generated contract files and `definitionDir` is the directory with `default.nix`. Standalone builds can pass `src`, `contract`, `definitionDir` and the optional lists in one call. If that form omits `definitionDir`, it defaults to `src`.

## Display a value

Use `card::display::send(value)` to publish a live update. The value's C++
type selects a bundled view.

| Value accepted by `card::display::send` | View name | Display |
| --- | --- | --- |
| `std::string_view` (including string literals) | `text` | Plain text |
| `card::display::Markdown` | `markdown` | Markdown |
| `double`, `card::display::Scalar` | `markdown` | A number with 6 significant digits and an optional bold label |
| `card::display::Table` | `table` | Column names and rows of strings |
| `card::display::Image` | `image` | Media type, image bytes and optional alt text |

Declare the bundled views and any custom views at the top level of the Card's
`default.nix`, alongside `run` and `exports`.

```nix
{
  run = sdk.cppCard { src = ./src; };
  views = sdk.views ++ [
    { name = "notes"; type = "html"; blob = ./notes.html; }
  ];
}
```

Each `blob` points to a single file. Use `type = "html"` for self-contained
HTML with any JavaScript and Wasm embedded, or `type = "dawn"` for a Wasm
binary targeting the Dawn host. The SDK currently bundles HTML views.

Use `card::display::send(view, bytes)` for a custom view. The view defines the
meaning of the bytes. The host routes them by the declared view name.

```cpp
const int status = card::display::send(
    "notes", card::display::text_bytes("Reading input"));
```

Keep the backing storage alive until `send` returns. A zero status means the
host accepted the update. An undeclared view name returns a nonzero status.
The callback and its host context last for the process lifetime, so a
Card-owned thread can send its own data after `run` returns. Display
messages do not enter history.

An HTML view draws its empty state on load. It then receives a window
message with `{tsugumi: 1, port}`, where `port` is a transferred
`MessagePort`. Set `port.onmessage` to handle
`{type: "data", bytes: ArrayBuffer}` and
`{type: "run", state: "started" | "ended"}`. The view manages its own layout
and runs in an isolated frame without network access.

## Cancellation, deadlines and failures

During a call, any Card thread can poll `card::cancelled()` or read the remaining time in milliseconds with `card::remaining_ms()`. The reference SDK uses `-1` for a call with no deadline. Cancellation state is process-wide. Between calls these functions return `true` and `0`, respectively. A long-running loop should check cancellation between units of work.

Throw `tsugumi::Failure(kind, message)` for a structured failure. The default third argument, `std::source_location::current()`, captures the **construction site**. If a helper constructs the exception, give the helper its own default location and pass it through, as below. The reference SDK reports a definition-relative source path and line for Card sources and omits locations in external sources. An optional fourth argument identifies a notebook cell.

Here is a complete file-forwarding Card with a required File input `source` and required File output `original`:

```cpp
#include "card.hh"

#include <source_location>

namespace {

auto CheckCancellation(std::source_location where = std::source_location::current())
    -> void {
  if (card::cancelled()) {
    throw tsugumi::Failure(Tsugumi::Data::FailureKind::cancelled,
                           "Stopped before forwarding the file", where);
  }
}

}  // namespace

auto run(const Card::Inputs& input) -> Card::Output {
  CheckCancellation();
  Card::Output output;
  output.original.emplace().value = input.source.value;
  return output;
}
```

The adapter translates the exception into `CardResult.failure`. A reserved or unknown failure kind becomes `worker_failed` and its original numeric value is included in the message. The ABI return code is zero when a result carrying a failure was built. Nonzero means the adapter could not supply a result. Outputs of a failed call are discarded. The exact failure enum names are provisional SDK details.

## Files, directories and worker lifetime

A File or Directory input carries an **absolute path** in its `value` member. Treat inputs as read-only and use each path only during its invocation. With filesystem isolation enabled, the worker enforces read-only access. With isolation off, it supplies a private, writable copy to protect the stored original. An already-open file descriptor is not revoked when the path expires.

| Location | How to use it | Lifetime |
| --- | --- | --- |
| CWD, also `HOME` | Writable space for this Card's files and caches. Create new File and Directory outputs here. | The process. A reused worker keeps the contents. |
| `TMPDIR` | Writable scratch. Read the path from the environment. | The path is fixed for the process. Its contents are cleared for each invocation. |
| `/tmp` | On workers that remap paths (Linux bubblewrap, shared VM, remote), this is the same managed, disk-backed scratch as `TMPDIR`. | On native macOS only `TMPDIR` has the scratch guarantee. |

For a **new output**, choose a name under the CWD, create the regular file or directory and put its path in `output.<port>.emplace().value`. A Directory output may contain a tree of regular files. Links, paths under `TMPDIR`, and granted host paths are rejected for new outputs. There is no provided output directory or target path in the input.

Finish writing and close all output streams **before returning**, including writes on background threads. The worker snapshots newly created files after receiving the result and before clearing scratch or starting another call. Directory collection snapshots files individually. Each call has limits on total output bytes and entry count. To **forward** a File or Directory, return its input path unchanged, as in the example above. Tsugumi records the original stored path without copying it. A symlink to the input does not qualify.

The [CSV filter template](../templates/filter-counts/) does both in one call. Its output schema marks the two File ports with `tsugumi: "file"`. For a Directory port, use a table with the same string `value` field and mark the port with `tsugumi: "directory"`.

With `reuseWorker`, memory and CWD contents can survive across invocations and runs **of the same Card**. One worker takes one call at a time. It ends with the series of runs and is recreated when the definition, SDK, grants or a secret value changes. A call already executing when the definition changes finishes on the old version. A cache in `HOME` should therefore be safe to rebuild when the next call finds it missing.

## Secrets and grants

Read a declared secret by its declared name, for example `std::getenv("db_token")` from `<cstdlib>`. The worker injects it at process startup. Check for a null pointer before using the value and keep the secret out of displays and logs.

A person grants access to named host paths (read-only or read-write), the GUI or the network. Grants reach the Card through the process environment. For a named host path, the **provisional** spelling is `TSUGUMI_GRANT_<NAME>`, for example `std::getenv("TSUGUMI_GRANT_CALIB")`. Use the supplied path. Grant variable names and the declaration syntax for host paths and GUI grants are still open. An operation requiring a disabled grant fails inside the Card.

## Removed from the Card interface

ABI 9 exposes no run ID, invocation number or Card identity, including through the environment. There are no `Report` or `Run` arguments, init phase, `on_run_start`, `preRun` or `postRun`. Read typed parameters from the input and write diagnostics to stdout or stderr. The old `TSUGUMI_PARAMS_JSON` and `TSUGUMI_*_DIR` variables are gone. Port values and parameters cross the ABI as FlatBuffers. There is no JSON input/output API, reflection or `bfbs` in the Card library.

The upstream decisions describe the [Card ABI](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CardABI.md), [sandbox contract](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/SandboxContract.md) and [SDK boundary](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CoreAndSDK.md).
