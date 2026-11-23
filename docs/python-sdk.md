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
Use `tsugumi.display.send(value)` for a bundled view, or
`tsugumi.display.send(view, bytes)` for custom data. The SDK also exposes
the bundled value classes from `tsugumi`, including `LineChart`,
`LineSeries`, `Points` and `Plotly`.

```python
from tsugumi import display

display.send("Processing complete")
status, data = display.recv("controls", 500)
```

The returned status is `"message"`, `"timeout"` or `"cancelled"`.
Data is `bytes` for a message and `None` for the other statuses.
Display effects remain available to Python threads after `run` returns.
Use `tsugumi.cancelled()` and `tsugumi.remaining_ms()` inside the call.
Raise `tsugumi.Failure("output_invalid", message)` to return a typed failure.
