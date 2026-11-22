# Python SDK for Card ABI 9, revision 2

A Python Card puts its entry function in `src/main.py`. The SDK embeds
CPython in the Card's `lib/main.so`.

```nix
{ pkgs, params, inputs, schemas }:
let
  sdk = inputs.tsugumi.pythonSdk;
in
{
  run = sdk.pythonCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
```

The builder uses the current `_inputs.fbs`, `_outputs.fbs`, and
`_params.fbs` in `schemas`. Open the Card editor's SDK section or inspect
the build output's `share/tsugumi/api/` for `card.pyi`, the FlatBuffers
object stubs, and a README describing the current ports and parameters.

```python
from pathlib import Path

import tsugumi
from card import Input, Output


def run(input: Input) -> Output:
    text = input.require("text").value
    path = Path("message.txt")
    path.write_bytes((text or b"") + b"\n")
    tsugumi.send(str(path))
    return Output(file=path)
```

The [Python starter](../templates/python-write-text/) uses this entry with a
FlatBuffer `text` input and a Path `file` output.

## Inputs and outputs

Use `input.require("text")` to get the generated type `T`. If the value is
absent, the SDK returns `input_missing` and names the port, including when the
port is disconnected. Use `input.get("text")` or the generated attribute
`input.text` for `T | None`. Disconnected inputs are absent from the generated
attributes, and `get()` returns `None` for their names. Methods use the wire
name. Generated attributes append underscores where needed to avoid
keywords and collisions.

Path inputs are filesystem strings. Outputs accept strings and
`os.PathLike[str]` objects such as `pathlib.Path`. Return the input string
unchanged to forward an input. Create new files or directories under the
call's working directory and finish writing before returning their paths.

FlatBuffer ports use flatc's generated object types, for example
`outputs.result.Number.NumberT`. Ordinary FlatBuffers string accessors
return bytes. For an ordinary union, set its tag and value to the matching
generated variant. A mixed Merge appears as a Python union such as
`TemperatureT | ReadingT`. Use `isinstance` to select an input variant and
return a member object on an output of that union. The SDK verifies and
handles the nested buffers. Optional outputs default to `None`, and an
omitted required output produces `output_invalid`.

## Interpreter and tools

Use `python` to select a nixpkgs interpreter and `packages` to select packages
from it, for example `packages = ps: [ ps.matplotlib ];`. Declare executables
with `tools = [ pkgs.coreutils ];` to use them through `subprocess.run`.
[Runtime tools](../README.md#runtime-tools)

## Views and failures

Use `tsugumi.send(value)` for text, numbers, `Markdown`, `Scalar`, `Table`,
`Image`, `LineChart`, `Points`, and `Plotly`. Declare `views = sdk.views;`
alongside `run` in your Card's `default.nix`.

```python
tsugumi.send(tsugumi.LineChart([
    tsugumi.LineSeries("squared", "Squared", [0, 1, 2], [0, 1, 4]),
]))
tsugumi.send(tsugumi.Points([1, 0], [0, 1], [0, 0], colors=[0xff4040, 0x40aa40]))
```

Each line series takes equal-length finite x/y sequences. Point coordinates
are x/y/z sequences of the same length. Omit colors to use the default, or
provide one packed `0xRRGGBB` integer per point. The point view skips non-finite
positions.

Append custom views with `views = sdk.views ++ [ ... ];` and publish their
payloads with `tsugumi.send(view, bytes)`. The named view defines the bytes'
format. Argument and encoding errors raise exceptions before calling the host.
The SDK raises `RuntimeError` if the host cannot accept the message.
See the [view protocol](cpp-sdk.md#display-a-value).

Call `tsugumi.send(tsugumi.Plotly(fig))` with a Plotly figure, or pass a
figure dictionary to `tsugumi.Plotly`. Add `packages = ps: [ ps.plotly ];` to
construct figures with Plotly in Python. A JSON-compatible dictionary works
without that Python dependency. The HTML file embeds Plotly.js.

The SDK sets `MPLCONFIGDIR` under `HOME` before importing the Card.
With Matplotlib installed, use `tsugumi.send(tsugumi.mpl.figure(fig))`
after importing `tsugumi.mpl`. The adapter uses IPAexGothic as a fallback for
Japanese glyphs when rendering the figure. Card threads can send live updates
between calls. Between calls `tsugumi.cancelled()` returns true and
`tsugumi.remaining_ms()` returns zero.

Raise `tsugumi.Failure("validation_failed", message)` to control the failure
kind. Unhandled Python exceptions produce a `worker_failed` result. The
SDK reports the source line when it can identify a frame in the Card source.

Build `checks.<system>.pythonAbi9` to run the Python Card with the shared
fake host.
