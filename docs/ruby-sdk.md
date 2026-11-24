# Ruby Card SDK

This guide targets **the Card ABI (unreleased)**. Put
`main(input)` in `src/main.rb` and import the Ruby SDK in the
Card's `default.nix`.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/ruby> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./_inputs.fbs;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.rubyCard (common // { src = ./src; });
  api = sdk.rubyApi common;
  views = sdk.views;
}
```

`rubyApi` publishes `card.rbs` and a typed README under `share/tsugumi/api/`.
Use **Build SDK** in the Card editor to inspect the API for the current wiring.

```ruby
def main(input)
  text = Tsugumi.params.fetch('value')
  file, directory = Tsugumi::Artifact.dir
  File.write(File.join(directory, 'note.txt'), text + "\n")
  {file: file}
end
```

The [Ruby starter](../templates/ruby-write-text/) uses this entry with a
text parameter and an Artifact `file` output.

## Inputs and outputs

Read a connected input with `input.fetch(:port)`, or use
`input[:port]` to get `nil` for a disconnected port. Build-time
`Tsugumi.params` is a frozen Hash with string keys and native values.

A mixed Merge yields a tagged Hash. Read `arrival['type']` for the
candidate name and `arrival['value']` for its table. The SDK selects and
verifies the candidate by schema ID. Collect yields opaque envelopes
with `schema` and `bytes`. Return a Hash of outputs.
Omit an optional output or set it to `nil`. A missing required output
produces `output_invalid`.

`Tsugumi::Artifact.dir` returns the Artifact and a writable directory.
`Tsugumi::Artifact.upload(path)` copies a file or directory.
`artifact.open` returns the read-only directory of any sealed Artifact of
the project whose ID the Card knows, such as one in an input. Forward an
Artifact by returning it in a declared Artifact field.

Both keep the Artifact after its run by default. With `keep: false` it
exists only while its run runs: later Cards of the same run can open it,
and it is removed with its record when the run ends. Opening it after that
raises an error saying the Artifact is not available. Read keep from a
parameter to let the person running choose:

```nix
# params.nix
{
  keep_scratch = false;
}
```

```ruby
scratch, directory = Tsugumi::Artifact.dir(keep: Tsugumi.params.fetch('keep_scratch'))
```

Declare Artifact fields with `include "tsugumi/artifact.fbs";`. The
host seals created directories when the call returns.

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

`Tsugumi.view_open(name, mode, flags, width, height)` returns `[route, session]`. Use
`Tsugumi.send(session, bytes)` to send one raw binary String and
`Tsugumi.route_register(session)` or `Tsugumi.route_unregister(route)` to
manage routes. `Tsugumi.send(value)` opens a panel and encodes the typed
values below. `Tsugumi.send_value(session, value)` sends a typed value to
an existing view. Argument and encoding
errors raise before calling the host. The SDK raises `RuntimeError` if the
host cannot accept the message.

Combine `Tsugumi::HIDE_CURSOR` and `Tsugumi::LOCK_POINTER` with `|`.
The default flags value is zero.

Declare the matching views alongside `run`. The Ruby SDK bundles text, markdown, table, image, line-chart, and points views.
Scalars use the `markdown` view.
[View protocol](sdk-contract.md#display-and-effects)

| Value | Constructor |
| --- | --- |
| Plain text and an unlabeled scalar | Ruby String or Numeric |
| Markdown and labeled scalar | `Tsugumi::Markdown.new(text)`, `Tsugumi::Scalar.new(number, label)` |
| Table | `Tsugumi::Table.new(columns, rows)` |
| Image | `Tsugumi::Image.new(binary_string, media_type, alt)` |
| Point cloud | `Tsugumi::Points.new(x, y, z, colors)`, with optional packed RGB integers |
| Line chart | `Tsugumi::LineChart.new([{id:, label:, x:, y:}, ...])` |
| Plotly and Vega-Lite | `Tsugumi::Plotly.new(config)`, `Tsugumi::VegaLite.new(config)` |

`Tsugumi.recv(timeout_ms)` returns `[kind, route, session, data]`. Kind is
`:message`, `:close`, `:timeout` or `:cancelled`. The final three values are
`nil` on timeout or cancellation. A negative timeout waits without a deadline. Ruby
threads can send and receive after `main` returns. `Tsugumi.cancelled`
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
