"""Python API for Cards."""

from _tsugumi_host import cancelled, remaining_ms

from . import _display as display
from ._display import (
    Image, LineChart, LineSeries, Markdown, OpenedView, Plotly, Points, Scalar, Table,
    ViewEvent, ViewFlags, ViewMode, recv, route_register, route_unregister, send, view_open,
)

__all__ = [
    "display", "Failure", "Image", "LineChart", "LineSeries", "Markdown", "Plotly",
    "Points", "Scalar", "Table",
    "OpenedView", "ViewEvent", "ViewMode", "ViewFlags", "cancelled", "recv", "remaining_ms",
    "route_register", "route_unregister", "send", "view_open",
]


class Failure(Exception):
    """Return a CardFailure with a FailureKind name and optional location."""

    def __init__(self, kind, message, *, source=None, line=0, cell=None):
        super().__init__(message)
        self.kind = kind
        self.source = source
        self.line = line
        self.cell = cell
