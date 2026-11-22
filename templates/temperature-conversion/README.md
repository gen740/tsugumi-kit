# Convert a temperature

An Input Card supplies degrees Celsius to a C++ Card that returns degrees Fahrenheit. **Targets unreleased Card ABI 9, revision 2.** The C++ Card will not build in released Tsugumi until the ABI 9 PR lands.

```text
settings.celsius → convert.celsius
                   convert.fahrenheit → downstream
```

Copy the pair into a project and connect a Trigger to `settings.start`. At the default input of 20 °C, `convert` returns 68 and shows a live markdown line labelled °F. Changing `settings` to −40 gives −40 °F.

The converter's `default.nix` declares the C++ SDK's bundled HTML views with `views = sdk.views`, including the `markdown` view used for the live display.

The example saves the internal wire using the existing project `tsugumi.json` canvas syntax. Its use as a template fragment is provisional. If your importer copies only definitions, connect `settings.celsius` to `convert.celsius` as shown. The optional [layout fragment](layout.json) places the two Cards side by side.

Git import subdirectory: `templates/temperature-conversion`. [Template guide](../../docs/templates.md) · [C++ source](convert/src/card.cc)
