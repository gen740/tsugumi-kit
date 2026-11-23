"""Encode values for the bundled views as a view name and bytes."""

import json
import math
import struct
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from functools import singledispatch
from numbers import Real
from typing import Protocol

import _tsugumi_host as _host


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


@dataclass(frozen=True)
class Points:
    x: Sequence[float]
    y: Sequence[float]
    z: Sequence[float]
    colors: Sequence[int] = ()


@dataclass(frozen=True)
class LineSeries:
    id: str
    label: str
    x: Sequence[float]
    y: Sequence[float]


@dataclass(frozen=True)
class LineChart:
    series: Sequence[LineSeries]


class _PlotlyFigure(Protocol):
    def to_plotly_json(self) -> Mapping: ...


@dataclass(frozen=True)
class Plotly:
    """A Plotly figure or its dictionary representation."""

    config: Mapping | _PlotlyFigure


_MISSING = object()
_JSON = {"ensure_ascii": False, "separators": (",", ":"), "sort_keys": True}


def _config(name, config, encoder=None):
    try:
        text = json.dumps(dict(config), allow_nan=False, **_JSON)
    except (TypeError, ValueError):
        if encoder is None:
            raise
        text = json.dumps(dict(config), cls=encoder(), **_JSON)
    return name, text.encode()


def _frame(name, config, *sections):
    name, metadata = _config(name, config)
    return name, b"".join((struct.pack("<I", len(metadata)), metadata, *sections))


def send(value, data=_MISSING):
    """Send a typed value or send(view, bytes) to a declared view."""
    if data is not _MISSING:
        _host.send(value, bytes(memoryview(data)))
    else:
        _host.send(*_encode(value))


@singledispatch
def _encode(value):
    raise TypeError(f"Cannot display a {type(value).__name__}")


@_encode.register
def _text(value: str):
    return _config("text", {"text": value})


@_encode.register
def _markdown(value: Markdown):
    return _config("markdown", {"text": value.text})


@_encode.register(Real)
def _number(value):
    return _scalar(Scalar(float(value)))


@_encode.register
def _scalar(value: Scalar):
    number = float(value.value)
    text = format(number, ".6g") if math.isfinite(number) else "\u2014"
    return _config("markdown", {"text": f"**{value.label}**: {text}" if value.label else text})


@_encode.register
def _table(value: Table):
    columns = list(map(str, value.columns))
    rows = [list(map(str, row)) for row in value.rows]
    if any(len(row) != len(columns) for row in rows):
        raise ValueError("Display table rows must match the column count")
    return _config("table", {"columns": columns, "rows": rows})


@_encode.register
def _image(value: Image):
    return _frame("image", {"media_type": value.media_type, "alt": value.alt},
                  bytes(value.data))


@_encode.register
def _points(value: Points):
    count = len(value.x)
    colors = len(value.colors)
    if len(value.y) != count or len(value.z) != count or colors not in (0, count):
        raise ValueError("Points require matching x, y, z and optional RGB colors")
    return _frame("points", {"count": count},
                  struct.pack(f"<{count}f", *value.x),
                  struct.pack(f"<{count}f", *value.y),
                  struct.pack(f"<{count}f", *value.z),
                  struct.pack(f"<{colors}I", *value.colors))


@_encode.register
def _line_chart(value: LineChart):
    series, x, y = [], bytearray(), bytearray()
    offset = 0
    for row in value.series:
        count = len(row.x)
        if len(row.y) != count or not all(map(math.isfinite, row.x)) or not all(map(math.isfinite, row.y)):
            raise ValueError("Line chart axes require matching finite values")
        series.append({"id": row.id, "label": row.label, "offset": offset, "count": count})
        x.extend(struct.pack(f"<{count}d", *row.x))
        y.extend(struct.pack(f"<{count}d", *row.y))
        offset += count
    return _frame("line-chart", {"series": series}, x, y)


@_encode.register
def _plotly(value: Plotly):
    config = value.config
    if not isinstance(config, Mapping):
        config = config.to_plotly_json()
    return _config("plotly", config, encoder=_plotly_encoder)


def _plotly_encoder():
    # Plotly's encoder also handles NumPy arrays, dates and non-finite values.
    from plotly.utils import PlotlyJSONEncoder
    return PlotlyJSONEncoder


def recv(view: str, timeout: int = -1) -> tuple[str, bytes | None]:
    """Return (message, bytes), (timeout, None), or (cancelled, None)."""
    return _host.recv(view, timeout)
