# Ruby Card SDK

This guide targets **the Card ABI (unreleased)**. Put
`main(input)` in `src/main.rb` and use `inputs.tsugumi.rubySdk` in the
Card's `default.nix`.

```nix
{ pkgs, params, inputs, schemas }:
let
  sdk = inputs.tsugumi.rubySdk;
in
{
  run = sdk.rubyCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
```

The builder uses the current `_inputs.fbs`, `_outputs.fbs`, and
`_params.fbs` under `schemas`. Its output contains `lib/main.so` and
`share/tsugumi/api/`, with `card.rbs` and a README describing the
Card's inputs, outputs, and parameters. Use **Build SDK** in the Card editor
to see these files for the current wiring.

```ruby
def main(input)
  text = input.fetch(:text).fetch('value')
  file = 'note.txt'
  File.write(file, text + "\n")
  {file: file}
end
```

The [Ruby starter](../templates/ruby-write-text/) uses this entry with a
FlatBuffer `text` input and a Path `file` output.

## Inputs and outputs

- `input.fetch(:port)` reads a needed input. If the port is absent, the
  SDK raises `Tsugumi::Failure` with kind `input_missing` and names the port,
  including when it has no wire. `input[:port]` returns the value or `nil`.
  Both accept string and symbol names. `fetch(:port, fallback)` and
  `fetch(:port) { |name| ... }` supply defaults. The block receives the
  name as a string. Read parameters through `input.params`, a Hash with
  string keys.
- Return a Hash keyed by output port name. Output Hashes accept string and
  symbol keys. Omit optional outputs or set them to `nil`. Missing a
  required output produces `output_invalid`.
- A Path input is a staged absolute path as a Ruby String. A Path
  output takes a String or an object with `to_path`, such as
  `Pathname`. Finish writing and close files under the call's working
  directory before returning their paths. Return an input path unchanged to
  forward it. Path ports can carry regular files or directories.
- For a mixed-type Merge, the SDK verifies and decodes the selected member's
  nested FlatBuffer. Read its type name at `arrival['type']` and its
  decoded table at `arrival['value']`. The generated API lists the names
  and shapes that the port can yield. Use the same tagged Hash shape for
  a mixed-type Merge output.

## FlatBuffer values

Tables are Hashes with string keys on input. Vectors are arrays. Known enum
values use their names and unknown values use integers. A union uses
`{'type' => 'Variant', 'value' => payload}` or `nil` when empty. A
`[ubyte]` field uses base64. Use `Tsugumi.decode_bytes(value)` to read a
binary String and `Tsugumi.bytes(string)` to encode one. Non-finite input
floats use `'NaN'`, `'Infinity'`, and `'-Infinity'`. Output fields accept
these strings and the corresponding Ruby floats.

Ruby loads the Card on the first valid call and keeps its state for the
worker's lifetime. To report a failure, raise an exception or
`Tsugumi::Failure.new(kind, message, cell: optional_cell)`. The SDK reports
the Card-relative source location when available. To use a nested source
directory, set `sourcePrefix` to its path relative to the Card definition.

## Effects

`Tsugumi.send(view, bytes)` takes a view name and a binary String.
`Tsugumi.send(value)` encodes the typed values below. Argument and encoding
errors raise before calling the host. The SDK raises `RuntimeError` if the
host cannot accept the message.

Declare the matching views alongside `run`. The Ruby SDK bundles text, markdown, table, image, line-chart, and points views.
Scalars use the `markdown` view.
[View protocol](cpp-sdk.md#display-a-value)

| Value | Constructor |
| --- | --- |
| Plain text and an unlabeled scalar | Ruby String or Numeric |
| Markdown and labeled scalar | `Tsugumi::Markdown.new(text)`, `Tsugumi::Scalar.new(number, label)` |
| Table | `Tsugumi::Table.new(columns, rows)` |
| Image | `Tsugumi::Image.new(binary_string, media_type, alt)` |
| Point cloud | `Tsugumi::Points.new(x, y, z, colors)`, with optional packed RGB integers |
| Line chart | `Tsugumi::LineChart.new([{id:, label:, x:, y:}, ...])` |
| Plotly and Vega-Lite | `Tsugumi::Plotly.new(config)`, `Tsugumi::VegaLite.new(config)` |

Ruby threads can send displays after `main` returns. `Tsugumi.cancelled`
and `Tsugumi.remaining_ms` read the active call's state from any Ruby
thread. Between calls they return `true` and `0`.

## Dependencies

Ruby defaults to the project's `pkgs.ruby`. To use gems, supply
`lock = ./Gemfile.lock` and `hash`, the fixed-output hash of the cached
source gems. Put the Gemfile beside the lockfile and include the `ruby`
platform with `bundle lock --add-platform ruby`. Use `buildInputs` for
native libraries required by a locked gem. The builder resolves locked gems
at build time and the SDK activates them in the embedded interpreter.

The builder accepts `tools` for runtime executables.
[Runtime tools](../README.md#runtime-tools) · [Card SDK contract](sdk-contract.md)
