# C++ SDK

Import `<tsugumi-kit/sdk/cpp> { inherit pkgs; }` in the Card definition.
Call `sdk.cppCard` with the evaluated `params`, schema paths `inputs` and
`outputs`, and a `src` directory containing `.cc` files. Use the same
schema paths and params with `sdk.cppApi`. See the shared
[Card SDK contract](sdk-contract.md) for the definition and view declarations.

```cpp
#include "card.hh"

auto run(const Card::Inputs& input) -> Card::Output {
  Card::Output output;
  output.result.emplace().value = input.sample->value + Card::params.offset;
  return output;
}
```

The generator uses FlatBuffers native object types for port tables. Input
members reflect the connected ports. For a Merge with multiple candidate
types, use `std::visit` or `std::get_if` on the generated `std::variant`.
Collect exposes the fixed `tsugumi::builtin::CollectedT` and its envelope
items. Each item provides `schema`, `bytes` and `artifacts`. Output members
use `std::optional`. A field declared `(required)` must be populated.
`Card::params` is a constant `Card::Params` with scalar fields or
`std::vector<T>` fields for list params.

## Artifacts

In generated native types, `card::Artifact` is the native
`tsugumi.Artifact` table. Use it for table members, vectors and the active
member of an authored union.

```cpp
auto [artifact, directory] = card::Artifact::dir();
std::ofstream(directory / "report.txt") << "complete\n";
output.report = std::move(artifact);
```

Use `card::Artifact::upload(path)` to copy an existing file or directory.
On a granted input, `artifact.open()` returns its read-only directory.
Assign the input Artifact to an output to forward its ID. The SDK lists the
Artifact IDs when packing the output.

## Display

Return `views = sdk.views;` in the definition to declare the bundled HTML
views. Send a typed display value with `card::display::send`. The overload
`send(view, bytes)` sends arbitrary bytes to a declared view. It returns
zero on success. `card::display::recv(view, timeout_ms)` returns a
`Received` with owned bytes and a `ReceiveStatus` of `kMessage`,
`kTimeout` or `kCancelled`.

```cpp
card::display::send(card::display::Markdown{"**Complete**"});
const auto reply = card::display::recv("controls", 500);
```

Display effects remain available to Card threads after the call returns.
Use `card::cancelled()` and `card::remaining_ms()` inside the call.
Throw `tsugumi::Failure` with a `Tsugumi::Data::FailureKind` to return a
structured failure. A definition can also pass `buildInputs`, `cxxflags`
and `tools` directly to the run builder.
