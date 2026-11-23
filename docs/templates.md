# Copying a Card template

A template is a folder of Card definition directories. It can contain a single Card, several Cards, or part of a graph with internal connections. Copy a template when you want to edit its Cards or wire them into an existing project. The examples here target **the Card ABI (unreleased)**.

## What goes in the folder

Each Card gets its own definition directory, named for its local ID. Put the same definition files there that you would author for a project Card. For example:

```text
filter-counts/
  README.md
  filter/
    _card.json
    _outputs.fbs
    params.nix
    default.nix
    src/card.cc
```

For a wired project template, keep the generated `_inputs.fbs` in the
destination Card definition. Tsugumi regenerates it from the project's
wiring and upstream output types. The file is absent for unwired Cards.

A Card declares its views alongside `run` and `exports` in `default.nix`.
Set `views = sdk.views;` for the bundled views, or append your own entries
with `views = sdk.views ++ [ { name = "notes"; type = "html"; blob = ./notes.html; } ];`.
Each `blob` is one self-contained HTML file for `"html"` or one Wasm binary
for `"dawn"`. A Card sends opaque bytes to the view by name at runtime.
Both C++ templates include the bundled HTML views. The CSV filter uses
`LineChart` to preview counts by channel. For a Card that calls external
executables, pass `tools = [ pkgs.coreutils ];` inside the builder call and
import `pkgs` with `import <nixpkgs> { }` inside `default.nix`.

[Display a value](cpp-sdk.md#display) · [Runtime tools](../README.md#runtime-tools)

The Go and Rust starters use the bundled `text` view from their SDK.
Each takes a string from `settings` and returns a one-file Artifact.

A template may include a `layout.bin` fragment (the FlatBuffers layout a project keeps, `protocol/layout_document.fbs` in Tsugumi) with positions for its Cards. A graph's **connections** specify which output port feeds which input port. Layout coordinates only tell the UI where to draw the Cards. In the temperature example, both definition directories, the internal wire and the layout travel together.

On copy, the core allocates fresh Card IDs and remaps references in the copied graph and layout. Copying the temperature pair twice should produce two independent pairs, each with its own internal wire. Connect ports at the boundary of each copy to the rest of your project.

Project templates carry `tsugumi.json` and `tsugumi.lock`. The declaration
names nixpkgs and tsugumi-kit. The lock's `entries` list records each name,
its declared URL, the resolved revision and the `narHash`. Project templates
keep their Card definitions under `definitions/<id>/`. A project created
from the template keeps the declaration and lock files. Copied Cards use the destination project's
`nixPath`. See the
[temperature project](../templates/temperature-conversion/tsugumi.json), its
[lock](../templates/temperature-conversion/tsugumi.lock), and its
[layout](../templates/temperature-conversion/layout.bin).

## Import from Git

Under the v1 design, choose the destination store and supply three values. For the single-Card example, use:

| Import field | Value |
| --- | --- |
| Repository URL | `https://github.com/gen740/tsugumi-kit.git` |
| Ref | `main` (use a commit ID to select an exact version) |
| Subdirectory | `templates/filter-counts` |

The subdirectory is the **template root**. Choosing its inner `filter/` directory would select one level too deep. The [template index](../templates/README.md) lists the available examples, including `templates/java-write-text`. v1 supports public Git repositories only.

| Destination store | Scope |
| --- | --- |
| **Host** | Under Tsugumi's support folder. Use it for templates you want available across projects on that host. |
| **Project** | The project's `templates/` directory. Templates here can be versioned with the project. |

Importing adds a template to the chosen store. To use it, choose **From template…** in **New Card** and select the stored template to copy into the graph. The menu lists Project and Host separately, with nothing preselected. If both stores have the same template name, both entries are listed with their store labels.

The core imports, stores, lists and copies templates. A person or the Agent chooses the template. The core has no automatic selection or default per language. The Agent may list and copy existing templates. Adding or importing one requires a person's approval. Scaffolding a new Card is the Agent's job.

The [upstream template decisions](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CoreAndSDK.md#templates) describe the planned store and menu behavior.
