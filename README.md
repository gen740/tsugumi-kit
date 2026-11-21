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
| `templates/` | Example templates. A template is a folder of Card definition directories (one Card or several, or part of a graph) with an optional `layout.json` fragment. Import it into Tsugumi's host or project template store from Git. |
| `sdk/<language>/` | One SDK per language (moving here from the Tsugumi repository). |

## Status

Just created. The SDKs are first made self-contained inside the Tsugumi
repository and then moved here; C++ is the reference SDK and comes first. The
decided design is in Tsugumi's `docs/CoreAndSDK.md`, `docs/CardABI.md` and
`docs/SandboxContract.md`.

Nothing here is stable before Tsugumi v1.0.0.
