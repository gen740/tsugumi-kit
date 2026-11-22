"""Encode values for the bundled views as a view name and named blobs."""

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
    """A display for the entry ``name`` in the Card's views.

    The view module defines the blob names and bytes, such as a ``config`` blob.
    """

    name: str
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


_JSON = {"ensure_ascii": False, "separators": (",", ":"), "sort_keys": True}


def _config(name, config, blobs=None, encoder=None):
    """Select a bundled view, which reads a "config" blob of UTF-8 JSON.

    encoder returns a JSON encoder class for values the standard one rejects.
    """
    try:
        text = json.dumps(dict(config), allow_nan=False, **_JSON)
    except (TypeError, ValueError):
        if encoder is None:
            raise
        text = json.dumps(dict(config), cls=encoder(), **_JSON)
    return Module(name, {"config": text.encode(), **(blobs or {})})


def display(value, *, record=False):
    """Display a value live, with optional recording for this update."""
    module = _as_module(value)
    blobs = tuple((str(name), bytes(data)) for name, data in module.blobs.items())
    _host.display(module.name, blobs, record)


@singledispatch
def _as_module(value):
    raise TypeError(f"Cannot display a {type(value).__name__}")


@_as_module.register
def _module(value: Module):
    return value


@_as_module.register
def _text(value: str):
    return _config("text", {"text": value})


@_as_module.register
def _markdown(value: Markdown):
    return _config("markdown", {"text": value.text})


@_as_module.register(Real)
def _number(value):
    return _scalar(Scalar(float(value)))


@_as_module.register
def _scalar(value: Scalar):
    number = float(value.value)
    text = format(number, ".6g") if math.isfinite(number) else "\u2014"
    return _config("markdown", {"text": f"**{value.label}**: {text}" if value.label else text})


@_as_module.register
def _table(value: Table):
    columns = list(map(str, value.columns))
    rows = [list(map(str, row)) for row in value.rows]
    if any(len(row) != len(columns) for row in rows):
        raise ValueError("Display table rows must match the column count")
    return _config("table", {"columns": columns, "rows": rows})


@_as_module.register
def _image(value: Image):
    return _config("image", {"media_type": value.media_type, "alt": value.alt},
                   {"image": value.data})


@_as_module.register
def _plotly(value: Plotly):
    config = value.config
    if not isinstance(config, Mapping):
        config = config.to_plotly_json()
    return _config("plotly", config, encoder=_plotly_encoder)


def _plotly_encoder():
    # Plotly's encoder also handles NumPy arrays, dates and non-finite values.
    from plotly.utils import PlotlyJSONEncoder
    return PlotlyJSONEncoder
