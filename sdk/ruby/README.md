# Ruby Cards for ABI 9

Build a Card with `tsugumi-kit.lib.${system}.rubyCard { src; contract; }`. The builder returns a derivation containing `lib/main.so`. Put `main.rb` in `src`. The generated contract directory contains `_contract.json`, `_inputs.fbs`, `_outputs.fbs` and `_params.fbs`. Set `sourcePrefix = "src";` when the source is in a directory called `src` relative to the Card definition. This prefix is used in error locations.

A Card defines one method. Input ports and parameters come from the conversion of the Card's FlatBuffer contract. Return a Hash indexed by output port name with a Hash per port. Both string and symbol keys work in output Hashes. Omit an optional output by leaving out its key.

```ruby
def main(input)
  if Tsugumi.cancelled
    raise Tsugumi::Failure.new(:cancelled, 'Stopped before converting temperature')
  end

  value = input.fetch(:celsius).fetch('value') * 1.8 + 32
  Tsugumi.display(Tsugumi::Scalar.new(value, '°F'), record: true)
  {fahrenheit: {value: value}}
end
```

- Read ports with `input[:port]` or `input.fetch(:port)`. The first returns `nil` for an absent port. Use `input.fetch(:port, fallback)` or `input.fetch(:port) { |name| fallback }` to supply a default. The block receives the port name as a string. You can also use `input.port` for present ports whose name is available as a Ruby method. Parameters are in `input.params`, a Hash with string keys.
- A table is a Hash with string keys on input. Vectors are arrays, enums use their member names, and unions use `{'type' => 'Variant', 'value' => payload}` or `nil` for an empty union. `[ubyte]` fields use base64. Use `Tsugumi.decode_bytes(value)` to get a binary String and `Tsugumi.bytes(string)` to encode one. Non-finite input floats use `'NaN'`, `'Infinity'` and `'-Infinity'`. Output float fields accept these tokens and the corresponding Ruby floats.
- `input.path(:file)` reads a File or Directory path. Create new outputs under the working directory, close the files before returning, and return `{port: {value: path}}`. The worker collects regular files and directories containing regular files. To forward an existing input, return its path unchanged.
- Ruby and the Card load once on the first valid request and keep their state for the worker's lifetime. To report a failure, raise an exception or `Tsugumi::Failure.new(kind, message, cell: optional_cell)`. Card exceptions include their Card-relative source location when available.

## Effects

`Tsugumi.display(value, record: false)` chooses a view from the value's type. The flag applies to the individual display. Use `record: true` to keep it in history.

| Value | Constructor |
| --- | --- |
| Plain text and an unlabeled scalar | Ruby String or Numeric |
| Markdown and labeled scalar | `Tsugumi::Markdown.new(text)`, `Tsugumi::Scalar.new(number, label)` |
| Table | `Tsugumi::Table.new(columns, rows)` |
| Image | `Tsugumi::Image.new(binary_string, media_type, alt)` |
| Point cloud | `Tsugumi::Points.new(x, y, z, colors)`, with optional packed RGB integers |
| Line chart | `Tsugumi::LineChart.new([{id:, label:, x:, y:}, ...])` |
| Plotly and Vega-Lite | `Tsugumi::Plotly.new(config)`, `Tsugumi::VegaLite.new(config)` |
| Custom view | `Tsugumi::View.new(view, blobs)`, with named binary Strings whose names and bytes the view module defines |

The other values send a `config` blob of UTF-8 JSON, followed by any data blobs.

Ruby threads can send displays after `main` returns. `Tsugumi.cancelled` and `Tsugumi.remaining_ms` read the active call's state from any Ruby thread. Between calls they return `true` and `0`.

## Dependencies

Ruby comes from the flake's Nixpkgs. To use gems, supply `lock = ./Gemfile.lock` and the fixed-output hash of the cached source gems as `hash`. The Gemfile must be beside the lock and the lock must include the `ruby` platform (`bundle lock --add-platform ruby`). Use `buildInputs` for native libraries required by a locked gem. The builder resolves locked gems ahead of time and the SDK activates them in the embedded interpreter.
