# Dawn view SDK

Declare a Dawn view with the C++ kit's `dawnView` builder. `src` is a
directory containing C sources. The builder produces a freestanding wasm32
module with the WebGPU imports declared in `<tsugumi/tsugumi_view.h>`.
The [color template](../templates/dawn-view) shows the Card and view together.

```nix
views = [
  (sdk.dawnView {
    name = "preview";
    src = ./views;
  })
];
```

## Callbacks and input

Include `<tsugumi/dawn_helpers.h>` for the ABI declarations, named input
constants and `tsugumi_dawn_send_to_opener`. Define the required exports
`abi`, `start`, `resize` and `draw` with the `tsugumi_view_` prefix.
Implement the optional callbacks used by the view.

- `draw(target, present)` receives the presentation time. Return
  `TSUGUMI_DAWN_DRAW_AGAIN` to request another frame.
- `resize(width, height, scale, at)` receives the physical target dimensions
  and the ratio of physical pixels to logical pixels.
- `message(size, at)` receives a Card message. Copy exactly `size` bytes
  with `tsugumi_view_read` while in the callback.
- `audio(frames, heard)` returns an interleaved stereo `float` buffer of
  `frames * 2` samples or `NULL` for silence. Keep the buffer valid after
  returning so the host can copy it. `heard` includes output latency.
- `pointer(..., at)` reports logical coordinates or relative movement
  during pointer lock. `key(..., at)` reports physical USB HID keys.
- `text_input()` returns a pointer to a logical-pixel IME rectangle kept
  alive after the callback, or `NULL` when editing is inactive.
  `text(size, at)` and `composition(size, cursor, at)` read UTF-8 with
  `tsugumi_view_read`. The cursor is a byte offset and a zero-length
  composition ends the preedit.
- `gamepad(..., at)` uses the W3C standard gamepad button and axis layout.

Times are unsigned 64-bit frame indices at 48 kHz since the view started.
Use `tsugumi_dawn_seconds(frames)` when an animation needs elapsed seconds.
To place a sound at a given frame, use its distance from `heard` as the
offset into the audio buffer.

## Embedded glyph atlas

Set `font = { };` to embed a 24-pixel Noto Sans CJK JP atlas containing
printable Windows-31J characters supported by the font. This covers Latin
text, hiragana, katakana and common kanji. You can set `font.pixelSize` to
another integer in 8–128 or set `font.characters` to a UTF-8 string of
exactly the characters your view needs. The replacement glyph is always
included. The build reports an error if the selected glyphs exceed the
4096-by-4096 atlas limit. The bitmap and glyph metrics travel in the Wasm
file and can be used without network or filesystem access. The source
typeface's copyright and license accompany the atlas in the Wasm custom
section `tsugumi.noto-sans-cjk-jp.ofl`.
[SIL Open Font License](https://github.com/notofonts/noto-cjk/blob/Sans2.004/LICENSE)

```c
#include <tsugumi/dawn_font.h>

// For example, in tsugumi_view_start.
WGPUTexture atlas = tsugumi_dawn_font_create_texture(device, queue);
if (!atlas) return -1;

const tsugumi_dawn_font_metrics* font = tsugumi_dawn_font_get_metrics();
const tsugumi_dawn_glyph* glyph = tsugumi_dawn_font_glyph(0x65E5);  // 日
```

Decode UTF-8 to Unicode scalars before looking up glyphs. For a pen position
`(pen_x, baseline_y)`, place the glyph quad at
`(pen_x + glyph->left, baseline_y + glyph->top)` with its bitmap width and
height. Divide the glyph's atlas rectangle by `font->atlas_width` and
`font->atlas_height` to derive UV coordinates. Sample the texture's red
channel as coverage. Advance the pen by `glyph->advance` even for blank
glyphs with a zero-area rectangle. Missing characters use U+FFFD. The
helper provides glyphs and metrics for horizontal layout. Layout, shaping
and rendering are view code.

Create and cache the atlas once per view instance. The texture uses
`R8Unorm` and is owned by the caller. Release it with `wgpuTextureRelease`
when finished.
