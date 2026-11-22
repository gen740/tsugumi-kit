# Writing a C++ Card

This guide targets **Card ABI 9 (unreleased)**. The C++ interface below follows the reference implementation in Tsugumi's `sdk/abi9` at `b7778919e`. The released worker cannot run these Cards yet.

## Return a value from `run`

A Card has the shape `(input) -> output`. Include the generated `card.hh` and define one free function. Here is the complete source for a Card with a required input port `celsius` and a required output port `fahrenheit`, both tables with a `value:double` field.

```cpp
#include "card.hh"

auto run(const Card::Inputs& input) -> Card::Output {
  const double fahrenheit = input.celsius.value * 1.8 + 32.0;
  card::display(card::Scalar{fahrenheit, "°F"});
  Card::Output output;
  output.fahrenheit.emplace().value = fahrenheit;
  return output;
}
```

For an input of 20, the next Card receives 68 on the output port. The scalar also appears live. Returning the output and displaying a value are independent operations. The [temperature conversion template](../templates/temperature-conversion/) includes the input Card, both output schemas and the wire between them.

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

The core injects the builder as `inputs.tsugumi.cppCard` with the contract and Card definition directory already bound. Use it in the Card's `default.nix`.

```nix
{ inputs, ... }:
rec {
  run = inputs.tsugumi.cppCard { src = ./src; };
  inherit (run) views;
}
```

The builder also accepts optional `buildInputs` and `cxxflags` lists. SDK selection belongs in `default.nix`. Omit `language` from `_card.json`.

To construct the injected builder, use `tsugumi-kit.lib.${system}.cppCard { inherit contract definitionDir; }`. Here `contract` is the directory with the four generated contract files and `definitionDir` is the directory with `default.nix`. Standalone builds can pass `src`, `contract`, `definitionDir` and the optional lists in one call. If that form omits `definitionDir`, it defaults to `src`.

## Display a value

`card::display(value)` selects the view by C++ type and shows it live. To keep that particular display in history, pass `true` as the second argument. The flag defaults to `false` on **every call**. For example, `card::display("Reading input")` is live only and `card::display(card::Scalar{42, "rows"}, true)` records the scalar. Each recorded display appends a row with its module, config, blobs, arrival time, run and Card.

| Value accepted by the SDK | View name | Display |
| --- | --- | --- |
| `std::string_view` (including a string literal) | `text` | Plain text |
| `card::Markdown` | `markdown` | Markdown |
| `double`, `card::Scalar` | `scalar` | A number, optionally with a label |
| `card::Table` | `table` | Column names and rows of strings |
| `card::Image` | `image` | Media type, image bytes and optional alt text |
| `card::Module` | The supplied name | A custom view's config and optional named blobs |

`cppCard` bundles the five basic views as JavaScript modules pending the Wasm implementations. The top-level Card `views` maps view names to module derivations. The name emitted by `card::display` selects an entry in that map.

Pass `views = { ... };` to `cppCard` to add or override entries in `run.views`, or set `views = run.views // { ... };` in the Card definition. A JS module derivation supplies `$out/index.js` with a default export `mount(root, data)`. The host provides `{ config, blobs }` and dispatches `tsugumi:data` on `root` with replacement data in the event's `detail`. The module defines the config format. The five SDK views use JSON strings, and the host supplies named blobs as `Uint8Array` values.

Keep backing strings and arrays alive until `display` returns. A zero status indicates success.

A Card-owned thread may keep displaying after `run` returns while the process lives. Give the thread its own data. The display callback lasts for the process lifetime. Tsugumi associates a recorded late display with its run and Card at arrival and leaves the invocation unspecified.

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
