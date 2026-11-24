# Python SDK

Import `<tsugumi-kit/sdk/python> { inherit pkgs; }` in the Card definition.
Call `sdk.pythonCard` with the evaluated `params`, schema paths `inputs`
and `outputs`, and a `src` directory containing `main.py`. Use those same
schema paths and params with `sdk.pythonApi` for the generated type stubs.
Pass optional Python dependencies with `packages = ps: [ ps.numpy ];`.
See the shared [Card SDK contract](sdk-contract.md) for the definition.

```python
from card import Input, Output, params
from results.Number import NumberT


def run(input: Input) -> Output:
    sample = input.require("sample")
    return Output(result=NumberT(value=sample.value + params.offset))
```

The SDK uses FlatBuffers object types for schema tables. For a Merge with
multiple candidates, narrow the generated union with `isinstance`.
A Collect port decodes to a `CollectedT` whose items contain schema IDs,
payload bytes and Artifact IDs. Output properties default to `None`.
Populate every `(required)` output. The generated `params` is a frozen
`Params` instance with typed scalar fields and tuples for list values.
Python uses the pinned flatc generator, which supports single union fields
and vectors of tables. For a vector of union values, put the union in a
table and use a vector of that table.

## Artifacts

```python
from card import Artifact

artifact, directory = Artifact.dir()
(directory / "report.txt").write_text("complete\n")
result = Output(report=artifact)
```

Use `Artifact.upload(path)` to copy a file or directory into a new
Artifact. On a granted input, `artifact.open()` returns its read-only
`pathlib.Path`. To forward the same ID, return the Artifact in an output.
The SDK lists IDs from nested tables, vectors and active union members
when packing the output.

## Display

Return `views = sdk.views;` to declare the bundled HTML views.
Use `tsugumi.display.send(value)` to open a bundled view as a panel.
To send to an existing session, use `display.send(session, value)` for
an SDK-framed typed value or `display.send(session, bytes)` for raw data.
The SDK also exposes
the bundled value classes from `tsugumi`, including `LineChart`,
`LineSeries`, `Points` and `Plotly`.

```python
from tsugumi import display

opened = display.view_open("controls")
display.send(opened.session, b"ready")
event = display.recv(500)
```

`view_open` returns the opener route and session. Use `route_register(session)`
to create a route for an existing session and `route_unregister(route)` to
release it. `recv` returns a `ViewEvent` with `kind`, `route`, `session` and
`data`. Message and close events carry the 16-byte IDs and `bytes` data.
Close has empty data. Timeout and cancellation have `None` in those fields.
Display effects remain available to Python threads after `run` returns.
Use `tsugumi.cancelled()` and `tsugumi.remaining_ms()` inside the call.
Raise `tsugumi.Failure("output_invalid", message)` to return a typed failure.
