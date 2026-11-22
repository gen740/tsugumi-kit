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
| **_language_ SDK** (C++ SDK, Python SDK, …) | One swappable part for one language: the Nix builder that turns a Card's source and contract into `lib/main.so`, the code generator for the typed entry (`card.hh` and `tsugumi_main`), the runtime library (`card::display::send`, `cancelled()`, `remaining_ms()`, an embedded interpreter where needed) and its documentation. |
| **tsugumi-kit** | This repository: the default SDKs and the example templates. |

"SDK" always means one language's part. It never refers to this repository as a
whole or to Tsugumi's worker, which belongs to the core.

## Layout

| Path | Contents |
| --- | --- |
| [C++ SDK guide](docs/cpp-sdk.md) | Write a typed Card for ABI 9, display results and work with files in the worker. |
| [Java SDK guide](docs/java-sdk.md) | Build a typed Java Card with an embedded JVM. |
| [Template guide](docs/templates.md) | Template format, Git import and the host and project stores. |
| [Example templates](templates/README.md) | A [CSV filter](templates/filter-counts/) and a [two-Card temperature conversion](templates/temperature-conversion/). |
| `abi/9/` | Vendored Card ABI 9 header and FlatBuffers schemas. |
| `sdk/cpp/` | C++ Card code generator, adapter and static runtime. |
| `default.nix` | The SDK set for a given nixpkgs: `import ./. { inherit pkgs; }`. |
| `nix/` | Flake outputs, including the C++ Card builder and runtime package. |
| `tests/` | Example ABI 9 Card, fake host and compatibility checks. |

## Entry

`default.nix` takes `{ pkgs }` and returns the SDK set built with that
nixpkgs. The Tsugumi core imports it with the project's `pkgs`, so a Card
builds with the project's pin rather than the kit's. The set holds each
builder (`cppCard`, `pythonCard`, …), each SDK attribute set with its
`views`, the `cppRuntime` package and `bind { contract, definitionDir; }`,
which returns the SDK attribute sets with every builder bound to that
Card's contract. The core passes that result to the Card's `default.nix`
as `inputs.tsugumi`. The flake's `lib.${system}` is the same set built with
the kit's own nixpkgs.

## Runtime tools

Each Card builder accepts `tools`, a list of nixpkgs packages whose executable
files are installed into the Card's `bin` directory. For example:

```nix
{ inputs, pkgs, ... }:
let
  sdk = inputs.tsugumi.cppSdk;
in
{
  run = sdk.cppCard {
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
`rustSdk`, `csharpSdk`, `juliaSdk`, `rSdk`, `rubySdk`, and `javaSdk`.
Every SDK bundles the `line-chart` and `points` HTML views. C++, Python, and Java also
bundle `text`, `markdown`, `table`, and `image`. Python bundles `plotly`.

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
