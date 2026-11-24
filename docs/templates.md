# Copying a Card template

A template is a folder of Card definition directories. It can contain a single Card, several Cards, or part of a graph with internal connections. Copy a template when you want to edit its Cards or wire them into an existing project. The examples here target **the Card ABI (unreleased)**.

## What goes in the folder

Each Card gets its own definition directory, named for its local id. Card ids and named ports match `[a-z][a-z0-9_]{0,63}`. Put the same definition files there that you would author for a project Card. For example:

```text
filter-counts/
  README.md
  filter/
    _card.json
    README.md
    _outputs.fbs
    params.nix
    default.nix
    src/card.cc
```

Every Card definition includes a `README.md` with a non-empty `title` for the display name and a one-line `description`, which may be empty. Both fields belong to the front matter. Write a space after the colon for an empty description. The `_card.json` writes all the fields required for its type, including its `symbol`. The Code and Input templates specify `aarch64-linux` in `system` and declare `workerCapabilities` explicitly. See [Project files](https://github.com/gen740/Tsugumi/blob/main/docs/Reference.md) for the field sets.

`params.nix` holds the Card's params. A Card without params has no
`params.nix`; Tsugumi treats a missing file as `{ }`.

Keep the generated `_inputs.fbs` with a Code Card template for direct SDK
builds. Tsugumi regenerates the schema from the destination project's wiring
and upstream output types. The schema has an empty `Inputs` table when no
inputs are connected.

A Card declares its views alongside `run` and `exports` in `default.nix`.
Set `views = sdk.views;` for the bundled views, or append your own entries
with `views = sdk.views ++ [ { name = "notes"; type = "html"; blob = ./notes.html; } ];`.
Each `blob` is one self-contained HTML file for `"html"` or one Wasm binary
for `"dawn"`. The Card opens a declared view by name, then sends bytes to
the returned session. A typed send in the starters opens its bundled HTML
view in panel mode.
The temperature and CSV templates include the bundled HTML views. The CSV filter uses
`LineChart` to preview counts by channel. For a Card that calls external
executables, pass `tools = [ pkgs.coreutils ];` inside the builder call and
import `pkgs` with `import <nixpkgs> { }` inside `default.nix`.

[Display a value](cpp-sdk.md#display) · [Runtime tools](../README.md#runtime-tools)

The [Dawn view template](../templates/dawn-view/) adds a Wasm view with
`sdk.dawnView`. Its C++ Card sends an RGB colour and receives click coordinates.

The Go and Rust starters use the bundled `text` view from their SDK.
Each takes a string from `settings` and returns a one-file Artifact.

A template may include a `layout.bin` fragment (the FlatBuffers layout a project keeps, `protocol/layout_document.fbs` in Tsugumi) with positions for its Cards. A graph's **connections** specify which output port feeds which input port. Layout coordinates only tell the UI where to draw the Cards. In the temperature example, both definition directories, the internal wire and the layout travel together.

On copy, the core allocates fresh Card IDs and remaps references in the copied graph and layout. Copying the temperature pair twice should produce two independent pairs, each with its own internal wire. Connect ports at the boundary of each copy to the rest of your project.

Project templates carry `tsugumi.json` and `tsugumi.lock`. The manifest writes all required project fields explicitly, including `tsugumiVersion`. Use templates whose version matches the Tsugumi release that reads them. The declaration
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
