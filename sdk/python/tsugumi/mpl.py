"""Optional Matplotlib adapter for display sends."""

from io import BytesIO

from . import Image


def figure(value, *, alt="", **savefig_kwargs):
    """Render a Matplotlib Figure as an Image for tsugumi.send."""
    buffer = BytesIO()
    value.savefig(buffer, format="png", **savefig_kwargs)
    return Image(buffer.getvalue(), alt=alt)
