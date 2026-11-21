"""Encode values for the default view modules."""

import json
import math
import struct
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from functools import singledispatch
from numbers import Real

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


@dataclass(frozen=True)
class Plotly:
    config: Mapping


@dataclass(frozen=True)
class VegaLite:
    config: Mapping


def display(value, *, record=False):
    """Display a value live, with optional recording for this update."""
    module = _as_module(value)
    config = json.dumps(dict(module.config), ensure_ascii=False, allow_nan=False,
                        separators=(",", ":"), sort_keys=True)
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
    return Module("tsugumi:text", {"text": value})


@_as_module.register
def _markdown(value: Markdown):
    return Module("tsugumi:markdown", {"text": value.text})


@_as_module.register(Real)
def _number(value):
    return _scalar(Scalar(float(value)))


@_as_module.register
def _scalar(value: Scalar):
    number = float(value.value)
    return Module("tsugumi:scalar",
                  {"value": number if math.isfinite(number) else None, "label": value.label})


@_as_module.register
def _table(value: Table):
    columns = list(map(str, value.columns))
    rows = [list(map(str, row)) for row in value.rows]
    if any(len(row) != len(columns) for row in rows):
        raise ValueError("Display table rows must match the column count")
    return Module("tsugumi:table", {"columns": columns, "rows": rows})


@_as_module.register
def _image(value: Image):
    return Module("tsugumi:image", {"media_type": value.media_type, "alt": value.alt},
                  {"image": value.data})


def _little_endian(code, values):
    return struct.pack(f"<{len(values)}{code}", *values)


@_as_module.register
def _points(value: Points):
    count = len(value.x)
    if len(value.y) != count or len(value.z) != count or len(value.colors) not in (0, count):
        raise ValueError("Points need matching x, y and z lengths and zero or one color per point")
    return Module("tsugumi:points", {"count": count}, {
        "x": _little_endian("f", value.x),
        "y": _little_endian("f", value.y),
        "z": _little_endian("f", value.z),
        "colors": _little_endian("I", value.colors),
    })


@_as_module.register
def _line_chart(value: LineChart):
    series = []
    x, y = [], []
    for row in value.series:
        if len(row.x) != len(row.y) or not all(map(math.isfinite, (*row.x, *row.y))):
            raise ValueError("Line series need matching x and y lengths and finite coordinates")
        series.append({"id": row.id, "label": row.label, "offset": len(x), "count": len(row.x)})
        x.extend(row.x)
        y.extend(row.y)
    return Module("tsugumi:line-chart", {"series": series},
                  {"x": _little_endian("d", x), "y": _little_endian("d", y)})


@_as_module.register
def _plotly(value: Plotly):
    return Module("tsugumi:plotly", value.config)


@_as_module.register
def _vega_lite(value: VegaLite):
    return Module("tsugumi:vega-lite", value.config)
