# Card SDK contract

The Card editor and Agent inspect the API generated for the Card's definition
and current input wiring. The run build uses the same generated types.

## Nix entry

Tsugumi calls `default.nix` with `{ params, inputs }`.

| Argument | Value |
| --- | --- |
| `params` | The evaluated `params.nix` attribute set of current values, or `{ }` when the file is absent. |
| `inputs` | An attribute per connected input port. Its `type` is the store path of a one-port FlatBuffers catalog. A normal upstream also supplies `exports`, including through Branch, Fork and Collect. Merge and Draft supply `type` alone. |

Imports such as `<nixpkgs>` and `<tsugumi-kit/sdk/cpp>` resolve through the
project's `nixPath` declarations and corresponding `tsugumi.lock` entries.
Each SDK takes the caller's `pkgs` and exposes a Card builder, an API builder,
and its bundled `views`.

```nix
{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/cpp> { inherit pkgs; };
  common = { inherit inputs params; outputs = ./_outputs.fbs; };
in
{
  run = sdk.cppCard (common // { src = ./src; });
  api = sdk.cppApi common;
  views = sdk.views;
}
```

Return the worker library as `run` and the codegen-only derivation as `api`.
A definition may also return `views` and upstream `exports`. The worker
loads `lib/main.so` from `run`. **Build SDK** evaluates and builds
`(import ./. { inherit params inputs; }).api` in the Card's directory and
displays the generated code and typed README from `share/tsugumi/api/`.

All language builders take `inputs`, `outputs`, and optionally `params`.
`outputs` points to the authored FlatBuffers file. Choose any root table
name and namespace. Each field of that root table describes one output
port. The run builder also takes `src` and language-specific options.

## Parameters

Keep the current parameter values in an optional, flat `params.nix` file.

```nix
{
  seed = 7;
  thresholds = [ 0.5 1.0 ];
  cxxflags = [ "-O3" ];
}
```

Use literal booleans, integers, floats, strings, and non-empty homogeneous
lists of those types. The literal determines the type, so `10` is an integer
and `10.0` is a float. The Card editor saves value changes in `params.nix`.
Pass the evaluated `params` value to both builders. When calling a builder
outside Tsugumi, use `params = import ./params.nix;` or omit `params` for a
Card without parameters. Reference build options directly in `default.nix`,
for example `cxxflags = params.cxxflags;` in the run builder call.

Every parameter is encoded in `CardInput.params` as a `parameters.Params`
FlatBuffer. Fields are sorted by name and numbered from zero. Booleans map to
`bool`, integers to `long`, floats to `double`, strings to `string`, and lists
to vectors of their scalar type. The builders derive the schema from names
and types alone. A value change affects a build only when the Card uses that
value in its derivation, as with `cxxflags` above.

## Inputs may be absent

The builder combines the single-field `inputs.Inputs` catalogs from
`inputs.<port>.type` into one generated input catalog. A Draft port has an
empty provisional table and an unconnected port has no entry. A CardInput may omit an input. A Card
declares whether it needs an input in its own code. The SDK reports
`CardFailure.input_missing` with a message identifying the port when the Card
requests a needed value and it is absent. Optional inputs use the language's
absence value. The table gives the conventions for this revision's SDK ports.

| SDK | Needed | Optional |
| --- | --- | --- |
| C++ | `T` | `std::optional<T>` |
| Rust | `T` via `require_input(name, option)?` | `Option<T>` |
| Python | `input.require(name)` returns `T` | `input.get(name)` or a generated attribute returns `T \| None` |
| Go | `T` via `RequireInput[T](input, name)` | `*T` via a generated accessor or `OptionalInput[T](input, name)` |
| C# | `T` via `input.Get<T>(name)` | `T?` via a generated property or `input.Find<T>(name)` |
| Java | `T` via `input.get(name, Type.class)` | `Optional<T>` via a generated accessor or `input.find(name, Type.class)` |
| Ruby | `input.fetch(:port)` returns `T` | `input[:port]` returns `T \| nil` |
| R | `input[["port"]]` or `input$port` | `optional_input(input, "port")`, absent as `NULL` |
| Julia | `input[:port]` or `input.port` yields `T` | `get(input, :port, nothing)` yields `Union{T, Nothing}` |
| JavaScript | `input.need(name)` returns `T` | `input.ports[name]` returns `T \| undefined` |

A Path port is declared as `string (tsugumi: "path")`. Expose it as the
language's path type, or a string where the language uses strings for paths.
Read and write the path in `CardPort.path` with kind `Path` and empty `bytes`.
An input path is staged and absolute. Return a new regular file or directory
under the call's working directory, or return an input path unchanged to
forward it. A FlatBuffer port carries its payload in `CardPort.bytes`.

A mixed-type Merge uses `tsugumi.merge.Merged`. Expose its distinct member
types in the generated API and decode the nested FlatBuffer in the selected
box's `bytes` field inside the SDK. A same-type Merge forwards its input type
and bytes unchanged. Verify and decode SDK payloads in the SDK.

## Generator and checks

All generators use the shared [port reflection module](../sdk/codegen/tsugumi_ports.py)
to read the assembled input catalog, authored output catalog, and generated
parameter catalog. The Nix checks call Card definitions with evaluated
parameter values, per-port files, and the project's nixPath names, then
exercise `tsugumi_main` through the vendored ABI. Each SDK check compares
the `run` and `api` derivation paths across values of the same type. The
`sdkApi` check builds `api` and verifies the typed README.
