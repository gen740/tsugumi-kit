"""Rasterize a Noto Sans CJK JP glyph atlas for a freestanding Dawn module."""

import argparse
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageFont

FALLBACK = 0xFFFD
GETA_MARK = 0x3013
MAX_EXTENT = 4096


def japanese_codepoints():
    """Printable single and double-byte Windows-31J characters."""
    for lead in (*range(0x20, 0x7F), *range(0xA1, 0xE0)):
        yield ord(bytes([lead]).decode("cp932"))
    for lead in (*range(0x81, 0xA0), *range(0xE0, 0xFD)):
        for trail in (*range(0x40, 0x7F), *range(0x80, 0xFD)):
            try:
                value = bytes([lead, trail]).decode("cp932")
            except UnicodeDecodeError:
                continue
            if value.isprintable():
                yield ord(value)


def rasterize(font, codepoint):
    char = chr(codepoint)
    mask, (left, top) = font.getmask2(char, mode="L", anchor="ls")
    width, height = mask.size
    # Quantize advance to FreeType's 26.6 pixel units.
    advance = round(font.getlength(char) * 64)
    if not width or not height:
        return None, 0, 0, advance
    return Image.frombytes("L", mask.size, bytes(mask)), left, top, advance


def pack(bitmaps, width):
    """Pack glyphs on shelves with a one-texel border for linear sampling."""
    x = y = row_height = 0
    positions = {}
    for codepoint, image in sorted(
        bitmaps.items(), key=lambda item: (-item[1].height, item[0])
    ):
        if image.width + 2 > width:
            return None
        if x + image.width + 2 > width:
            x = 0
            y += row_height
            row_height = 0
        positions[codepoint] = x + 1, y + 1
        x += image.width + 2
        row_height = max(row_height, image.height + 2)
    height = max(1, y + row_height)
    return (height, positions) if height <= MAX_EXTENT else None


def uleb128(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return result


def generate(font_path, pixel_size, characters_path, license_path):
    font = ImageFont.truetype(font_path, pixel_size, layout_engine=ImageFont.Layout.BASIC)
    with TTFont(font_path) as source:
        supported = source.getBestCmap()
        copyright_notice = source["name"].getDebugName(0)
    name = b"tsugumi.noto-sans-cjk-jp.ofl"
    notice = copyright_notice.encode("utf-8") + b"\n\n" + license_path.read_bytes()
    section = uleb128(len(name)) + name + notice
    Path("font-license.wasm-section").write_bytes(b"\0" + uleb128(len(section)) + section)
    characters = (
        set(japanese_codepoints())
        if characters_path is None
        else set(map(ord, characters_path.read_text(encoding="utf-8")))
    )
    # The JP face uses the Japanese missing-character mark for replacement.
    glyph_for = {codepoint: codepoint for codepoint in characters & supported.keys()}
    glyph_for[FALLBACK] = FALLBACK if FALLBACK in supported else GETA_MARK
    glyphs = {
        codepoint: rasterize(font, glyph_for[codepoint])
        for codepoint in sorted(glyph_for)
    }
    bitmaps = {codepoint: glyph[0] for codepoint, glyph in glyphs.items() if glyph[0]}

    candidates = []
    for width in (256, 512, 1024, 2048, MAX_EXTENT):
        result = pack(bitmaps, width)
        if result is not None:
            height, positions = result
            candidates.append((width * height, width, height, positions))
    if not candidates:
        raise ValueError("Glyphs do not fit within a 4096 x 4096 texture")
    _, width, height, positions = min(candidates, key=lambda item: item[:3])
    atlas = Image.new("L", (width, height))
    for codepoint, image in bitmaps.items():
        atlas.paste(image, positions[codepoint])
    Path("font-atlas.bin").write_bytes(atlas.tobytes())

    ascent, descent = font.getmetrics()
    lines = [
        "static const tsugumi_dawn_font_metrics metrics = {"
        f"{width}, {height}, {pixel_size}, {ascent}, {descent}" "};",
        "static const struct { uint32_t codepoint; tsugumi_dawn_glyph glyph; } glyphs[] = {",
    ]
    for codepoint, (image, left, top, advance) in glyphs.items():
        x, y = positions.get(codepoint, (0, 0))
        w, h = image.size if image else (0, 0)
        lines.append(
            f"  {{0x{codepoint:X}, {{{x}, {y}, {w}, {h}, {left}, {top}, {advance} / 64.0f}}}},"
        )
    lines.extend(
        ("};", f"enum {{ fallback_glyph = {list(glyphs).index(FALLBACK)} }};", "")
    )
    Path("font-metrics.h").write_text("\n".join(lines), encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("pixel_size", type=int, choices=range(8, 129), metavar="8..128")
    parser.add_argument("--characters", type=Path)
    parser.add_argument("--license", type=Path, required=True)
    args = parser.parse_args()
    generate(args.font, args.pixel_size, args.characters, args.license)
