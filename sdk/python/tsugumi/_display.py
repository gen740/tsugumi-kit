"""Encode values for the default view modules."""

import json
import math
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from functools import singledispatch
from numbers import Real
from typing import Protocol

import _tsugumi_host as _host


@dataclass(frozen=True)
class Module:
    name: str
    config: Mapping
    blobs: Mapping[str, bytes] = field(default_factory=dict)


@dataclass(frozen=True)
class Markdown:
    text: str


@dataclass(frozen=True)
class Scalar:
    value: float
    label: str = ""


@dataclass(frozen=True)
class Table:
    columns: Sequence[str]
    rows: Sequence[Sequence]


@dataclass(frozen=True)
class Image:
    data: bytes
    media_type: str = "image/png"
    alt: str = ""


class _PlotlyFigure(Protocol):
    def to_plotly_json(self) -> Mapping: ...


@dataclass(frozen=True)
class Plotly:
    """A Plotly figure or its dictionary representation."""

    config: Mapping | _PlotlyFigure


def display(value, *, record=False):
    """Display a value live, with optional recording for this update."""
    module = _as_module(value)
    options = {"ensure_ascii": False, "separators": (",", ":"), "sort_keys": True}
    payload = dict(module.config)
    try:
        config = json.dumps(payload, allow_nan=False, **options)
    except (TypeError, ValueError):
        if not isinstance(value, Plotly):
            raise
        # Plotly's encoder also handles NumPy arrays, dates and non-finite values.
        from plotly.utils import PlotlyJSONEncoder
        config = json.dumps(payload, cls=PlotlyJSONEncoder, **options)
    blobs = tuple((name, bytes(data)) for name, data in module.blobs.items())
    _host.display(module.name, config, blobs, record)


@singledispatch
def _as_module(value):
    raise TypeError(f"Cannot display a {type(value).__name__}")


@_as_module.register
def _module(value: Module):
    return value


@_as_module.register
def _text(value: str):
    return Module("text", {"text": value})


@_as_module.register
def _markdown(value: Markdown):
    return Module("markdown", {"text": value.text})


@_as_module.register(Real)
def _number(value):
    return _scalar(Scalar(float(value)))


@_as_module.register
def _scalar(value: Scalar):
    number = float(value.value)
    text = format(number, ".6g") if math.isfinite(number) else "\u2014"
    return Module("markdown", {"text": f"**{value.label}**: {text}" if value.label else text})


@_as_module.register
def _table(value: Table):
    columns = list(map(str, value.columns))
    rows = [list(map(str, row)) for row in value.rows]
    if any(len(row) != len(columns) for row in rows):
        raise ValueError("Display table rows must match the column count")
    return Module("table", {"columns": columns, "rows": rows})


@_as_module.register
def _image(value: Image):
    return Module("image", {"media_type": value.media_type, "alt": value.alt},
                  {"image": value.data})


@_as_module.register
def _plotly(value: Plotly):
    config = value.config
    if not isinstance(config, Mapping):
        config = config.to_plotly_json()
    return Module("plotly", config)
