"""Optional Matplotlib adapter for display sends."""

from io import BytesIO
from pathlib import Path

from matplotlib import font_manager, rc_context, rcParams
from matplotlib.text import Text

from . import Image

_font_file = Path(__file__).with_name("ipaexg.ttf")
_fallback = None
if _font_file.is_file():
    font_manager.fontManager.addfont(_font_file)
    _fallback = font_manager.FontProperties(fname=_font_file).get_name()


def _with_fallback(families):
    if _fallback is None or _fallback in families:
        return families
    return [*families, _fallback]


def figure(value, *, alt="", **savefig_kwargs):
    """Render a Matplotlib Figure as an Image for tsugumi.send."""
    buffer = BytesIO()
    # Existing artists already copied their family list from rcParams. Apply
    # the fallback to them as well as to text created during savefig.
    original = [(text, text.get_fontfamily()) for text in value.findobj(Text)]
    try:
        for text, families in original:
            text.set_fontfamily(_with_fallback(families))
        with rc_context({"font.family": _with_fallback(rcParams["font.family"])}):
            value.savefig(buffer, format="png", **savefig_kwargs)
    finally:
        for text, families in original:
            text.set_fontfamily(families)
    return Image(buffer.getvalue(), alt=alt)
