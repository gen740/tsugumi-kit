# Copying a Card template

A template is a folder of Card definition directories. It can contain a single Card, several Cards, or part of a graph with internal connections. Copy a template when you want to edit its Cards or wire them into an existing project. The examples here target **Card ABI 9 (unreleased)**.

## What goes in the folder

Each Card gets its own definition directory, named for its local ID. Put the same definition files there that you would author for a project Card. For example:

```text
filter-counts/
  README.md
  filter/
    _card.json
    _outputs.fbs
    default.nix
    src/card.cc
```

A template may include a `layout.json` fragment with positions for its Cards. A graph's **connections** specify which output port feeds which input port. Layout coordinates only tell the UI where to draw the Cards. In the temperature example, both definition directories, the internal wire and the layout travel together.

On copy, the core allocates fresh Card IDs and remaps references in the copied graph and layout. Copying the temperature pair twice should produce two independent pairs, each with its own internal wire. Connect ports at the boundary of each copy to the rest of your project.

The decided format specifies the folder, its contents and the remapping behavior. It leaves the on-disk representation of a template's connections unspecified. The two-Card example uses a **provisional** `tsugumi.json` fragment with the existing project `canvas.cards` and `canvas.connections` fields. Its README gives both endpoints of the wire.

## Import from Git

Under the v1 design, choose the destination store and supply three values. For the single-Card example, use:

| Import field | Value |
| --- | --- |
| Repository URL | `https://github.com/gen740/tsugumi-kit.git` |
| Ref | `main` (use a commit ID to select an exact version) |
| Subdirectory | `templates/filter-counts` |

The subdirectory is the **template root**. Choosing its inner `filter/` directory would select one level too deep. To import the two-Card example instead, use `templates/temperature-conversion`. v1 supports public Git repositories only.

| Destination store | Scope |
| --- | --- |
| **Host** | Under Tsugumi's support folder. Use it for templates you want available across projects on that host. |
| **Project** | The project's `templates/` directory. Templates here can be versioned with the project. |

Importing adds a template to the chosen store. To use it, choose **From template…** in **New Card** and select the stored template to copy into the graph. The menu lists Project and Host separately, with nothing preselected. If both stores have the same template name, both entries are listed with their store labels.

The core imports, stores, lists and copies templates. A person or the Agent chooses the template. The core has no automatic selection or default per language. The Agent may list and copy existing templates. Adding or importing one requires a person's approval. Scaffolding a new Card is the Agent's job.

The [upstream template decisions](https://github.com/gen740/Tsugumi/blob/1610f0f934edbcc627babcfb6df59829e62d7c51/docs/CoreAndSDK.md#templates) describe the planned store and menu behavior.
