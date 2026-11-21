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
| **Card ABI** | The contract between Tsugumi's core and an SDK: `tsugumi_main` and the fixed `CardInput` / `CardResult` FlatBuffers. |
| **_language_ SDK** (C++ SDK, Python SDK, …) | One swappable part for one language: the Nix builder that turns a Card's source and contract into `lib/main.so`, the code generator for the typed entry (`card.hh` and `tsugumi_main`), the runtime library (`card::display`, `cancelled()`, `remaining_ms()`, an embedded interpreter where needed) and its documentation. |
| **tsugumi-kit** | This repository: the default SDKs and the example templates. |

"SDK" always means one language's part. It never refers to this repository as a
whole or to Tsugumi's worker, which belongs to the core.

## Layout

| Path | Contents |
| --- | --- |
| [C++ SDK guide](docs/cpp-sdk.md) | Write a typed Card for ABI 9, display results and work with files in the worker. |
| [Template guide](docs/templates.md) | Template format, Git import and the host and project stores. |
| [Example templates](templates/README.md) | A [CSV filter](templates/filter-counts/) and a [two-Card temperature conversion](templates/temperature-conversion/). |
| `abi/9/` | Vendored Card ABI 9 header and FlatBuffers schemas. |
| `sdk/cpp/` | C++ Card code generator, adapter and static runtime. |
| `nix/` | Flake outputs, including the C++ Card builder and runtime package. |
| `tests/` | Example ABI 9 Card, fake host and compatibility checks. |

## Status

The guides and examples target **Card ABI 9 (unreleased)**. The C++ reference
SDK is being developed in Tsugumi. The default SDKs move here after they are
self-contained. The upstream decisions cover the
[core and SDK boundary](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CoreAndSDK.md),
[Card ABI](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CardABI.md)
and [sandbox contract](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/SandboxContract.md).

Nothing here is stable before Tsugumi v1.0.0.
