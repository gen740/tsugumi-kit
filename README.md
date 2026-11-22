# tsugumi-kit

The default SDKs and example templates for [Tsugumi](https://github.com/gen740/Tsugumi).

Tsugumi's core knows no programming language. A Card reaches the core only
through the **Card ABI**: a `lib/main.so` that exports `tsugumi_main`, built by a
Nix entry point that the Card's `default.nix` calls. Everything language-specific
lives in an SDK, and any SDK that implements the Card ABI can be used, including
one you write yourself.

## Terms

| Term | Meaning |
| --- | --- |
| **Card ABI** | The contract between Tsugumi's core and an SDK: `tsugumi_main` and the fixed `CardInput` / `CardResult` FlatBuffers and the host effects table. |
| **_language_ SDK** (C++ SDK, Python SDK, …) | One swappable part for one language: the Nix builder that turns a Card's source and schemas into `lib/main.so`, the code generator for the typed entry (`card.hh` and `tsugumi_main`), the runtime library (`card::display::send`, `cancelled()`, `remaining_ms()`, an embedded interpreter where needed) and its documentation. |
| **tsugumi-kit** | This repository: the default SDKs and the example templates. |

"SDK" always means one language's part. It never refers to this repository as a
whole or to Tsugumi's worker, which belongs to the core.

## Layout

| Path | Contents |
| --- | --- |
| [C++ SDK guide](docs/cpp-sdk.md) | Write a typed Card for ABI 9, display results and work with files in the worker. |
| [C# SDK guide](docs/csharp-sdk.md) | Build a typed C# Card with the .NET runtime. |
| [Python SDK guide](docs/python-sdk.md) | Write a typed CPython Card for ABI 9 revision 2. |
| [Go SDK guide](docs/go-sdk.md) | Build a Go Card with generated types and offline module dependencies. |
| [Rust SDK guide](docs/rust-sdk.md) | Build a Rust Card with verified borrowed inputs and owned outputs. |
| [Julia SDK guide](docs/julia-sdk.md) | Write a Julia Card with generated port documentation and an embedded Julia runtime. |
| [R SDK guide](docs/r-sdk.md) | Write an R Card with named-list ports and an embedded R runtime. |
| [Ruby SDK guide](docs/ruby-sdk.md) | Write a Ruby Card with generated RBS signatures and locked gems. |
| [Java SDK guide](docs/java-sdk.md) | Build a typed Java Card with an embedded JVM. |
| [JavaScript SDK guide](docs/js-sdk.md) | Write an async Node.js Card with generated TypeScript bindings and npm dependencies for ABI 9 revision 2. |
| [Template guide](docs/templates.md) | Template format, Git import and the host and project stores. |
| [Example templates](templates/README.md) | A [CSV filter](templates/filter-counts/) and a [two-Card temperature conversion](templates/temperature-conversion/). |
| `abi/9/` | Vendored Card ABI 9 header and FlatBuffers schemas. |
| `sdk/cpp/` | C++ Card code generator, adapter and static runtime. |
| `default.nix` | The SDK set for a given nixpkgs: `import ./. { inherit pkgs; }`. |
| `nix/` | Flake outputs, including the C++ Card builder and runtime package. |
| `tests/` | Example ABI 9 Card, fake host and SDK checks. |

## Entry

`default.nix` takes `{ pkgs }` and returns the SDK set built with that
nixpkgs. The Tsugumi core imports it with the project's `pkgs` and passes the
result as `inputs.tsugumi`. Each Card passes its `schemas` directory
explicitly to its SDK builder. The flake's `lib.${system}` is the same SDK set
built with the kit's own nixpkgs. [Card SDK contract](docs/sdk-contract.md)

## Runtime tools

Each Card builder accepts `tools`, a list of nixpkgs packages whose executable
files are installed into the Card's `bin` directory. For example:

```nix
{ pkgs, params, inputs, schemas }:
let
  sdk = inputs.tsugumi.cppSdk;
in
{
  run = sdk.cppCard {
    inherit schemas;
    src = ./src;
    tools = [ pkgs.coreutils ];
  };
  views = sdk.views;
}
```

The worker starts with `PATH` unset. When `tools` is nonempty, the SDK sets
`PATH` to the absolute path of the Card package's `bin` directory as
`lib/main.so` loads. An empty `tools` list leaves `PATH` unset.
If two packages provide the same command, the first package in `tools` wins.
Start commands directly with their argument arrays. To use shell syntax,
declare a shell package and invoke its executable
explicitly, for example `bash -c …`.

## Views

Each language SDK exposes a builder and a `views` list under
`lib.${system}`. The SDK attributes are `cppSdk`, `pythonSdk`, `goSdk`,
`rustSdk`, `csharpSdk`, `juliaSdk`, `rSdk`, `rubySdk`, `javaSdk`, and `jsSdk`.
Every SDK bundles the `line-chart` and `points` HTML views. C++, Python, Java, and
JavaScript also bundle `text`, `markdown`, `table`, and `image`. Python bundles `plotly`.

Declare `views = sdk.views ++ [ ... ];` alongside `run` in your Card's
`default.nix`. A view entry has a `name`, a `type` of `"html"` or
`"dawn"`, and a `blob` pointing to a single self-contained HTML file or
Wasm binary, respectively. The ABI's `send` effect delivers opaque bytes
to a view by name during or between calls. The core does not check the
name; the UI matches it against the Card's declared views and shows an
error for an unknown name. Display messages are live
and do not enter history. See the [view protocol](docs/cpp-sdk.md#display-a-value).

The typed text, markdown, table, and image encoders in the other SDKs can
use the corresponding C++ HTML files. For example, a Go Card that sends a
number can append the `markdown` view from `lib.${system}.cppSdk.views`.

## Status

The guides and examples target **Card ABI 9 (unreleased)**. The upstream decisions cover the
[core and SDK boundary](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CoreAndSDK.md),
[Card ABI](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CardABI.md)
and [sandbox contract](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/SandboxContract.md).

Nothing here is stable before Tsugumi v1.0.0.
