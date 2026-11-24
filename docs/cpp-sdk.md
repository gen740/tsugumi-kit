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

### Dawn views

Append a freestanding WebAssembly view to the Card's `views`:

```nix
views = sdk.views ++ [
  (sdk.dawnView { name = "clear"; src = ./views; })
];
```

`sdk.dawnView` compiles the directory's C sources into `view.wasm` and
accepts optional `cflags`. Include `<tsugumi/tsugumi_view.h>` for the
required callbacks, the allowed WebGPU functions and the message helpers.
The builder supplies the pinned WebGPU header. Only the `webgpu` and
`tsugumi` import modules are available. Implement
`tsugumi_view_message` to receive bytes and call `tsugumi_view_read` inside
that callback. Use `tsugumi_view_send` for replies. Each message is limited
to `TSUGUMI_MESSAGE_MAX` (16 MiB).

The view has no WASI, libc or `malloc`. Use static buffers or a bump
allocator backed by `__builtin_wasm_memory_grow`. The builder enables
`-mbulk-memory` (also the default for clang 21's generic wasm32 CPU), so
inlined memory copies and fills can use `memory.copy` and `memory.fill`.
For explicit copies or fills, use compiler builtins in place of libc
`memcpy` and `memset`.

See the [Dawn view template](../templates/dawn-view/) for a complete Card
and view.
