"""Python API for Cards."""

from _tsugumi_host import cancelled, remaining_ms

from ._display import Image, LineChart, LineSeries, Markdown, Plotly, Points, Scalar, Table, send

__all__ = [
    "Failure", "Image", "LineChart", "LineSeries", "Markdown", "Plotly",
    "Points", "Scalar", "Table",
    "cancelled", "send", "remaining_ms",
]


class Failure(Exception):
    """Return a CardFailure with a FailureKind name and optional location."""

    def __init__(self, kind, message, *, source=None, line=0, cell=None):
        super().__init__(message)
        self.kind = kind
        self.source = source
        self.line = line
        self.cell = cell
