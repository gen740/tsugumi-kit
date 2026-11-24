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
  output.result.emplace().value = input.sample->value() + Card::params.offset;
  return output;
}
```

Input members are pointers to verified FlatBuffers tables borrowed from
the host's input. They are valid during `run`. Use the generated accessor
methods to read scalars, strings, vectors and child tables. A Merge with
multiple candidate types is a `std::variant` of table pointers. Use
`std::visit` or `std::get_if` to select the actual table. Collect exposes a
`tsugumi::builtin::Collected` table whose items contain each original
schema and byte vector. Call `UnPack()` on a table when you need an owned
native value. Output members use `std::optional` of the native table
types. A field declared `(required)` must be populated.
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
`artifact.open()` returns the read-only directory of any sealed Artifact of
the project whose ID the Card knows. To open or forward an input
Artifact, unpack the borrowed table to an owned Artifact first.

`card::Artifact::dir(keep)` and `card::Artifact::upload(path, keep)` keep
the Artifact after its run by default. With `keep` false it exists only
while its run runs: later Cards of the same run can open it, and it is
removed with its record when the run ends. Opening it after that throws an
error saying the Artifact is not available. To let the person running
choose, read keep from a parameter:

```nix
# params.nix
{
  keep_scratch = false;
}
```

```cpp
auto [scratch, directory] = card::Artifact::dir(Card::params.keep_scratch);
```

## Display

Return `views = sdk.views;` in the definition to declare the bundled HTML
views. `card::display::send(value)` opens the matching bundled view as a
panel and sends an SDK-framed value. The `Points` and `LineChart` overloads
also accept a session for updates to an existing view.

`view_open` returns a route and session. Use `send(session, bytes)` for
one raw message, `route_register(session)` to subscribe to an existing
session, and `route_unregister(route)` to release a route. `recv(timeout_ms)`
returns a `Received` with status `kMessage`, `kClose`, `kTimeout` or
`kCancelled`. Message and close events carry the route and session, and
the returned bytes are owned by `Received`.

```cpp
using namespace card::display;
const auto view = view_open("controls");
if (send(view.session, text_bytes("ready")) != 0) {
  throw std::runtime_error("Could not send the view message");
}
const auto event = recv(500);
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
that callback. Pass a 16-byte route to `tsugumi_view_send(route, data, size)`
for replies. Use a zero-filled route to reach the opener. On the Card side,
`card::display::send_raw(session, bytes)` sends one message directly to
the view. It returns zero on success. Each message is limited to
`TSUGUMI_MESSAGE_MAX` (16 MiB).

The view has no WASI, libc or `malloc`. Use static buffers or a bump
allocator backed by `__builtin_wasm_memory_grow`. The builder enables
`-mbulk-memory` (also the default for clang 21's generic wasm32 CPU), so
inlined memory copies and fills can use `memory.copy` and `memory.fill`.
For explicit copies or fills, use compiler builtins in place of libc
`memcpy` and `memset`.

See the [Dawn view template](../templates/dawn-view/) for a complete Card
and view.
