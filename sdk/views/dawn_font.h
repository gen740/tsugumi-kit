#pragma once
#include <tsugumi/tsugumi_view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t atlas_width, atlas_height;
  uint32_t pixel_size;
  int32_t ascent, descent; /* Positive distances from the baseline in pixels. */
} tsugumi_dawn_font_metrics;

typedef struct {
  uint32_t x, y, width, height; /* Rectangle in atlas texels. */
  int32_t left, top;            /* Offset from pen and baseline, y down. */
  float advance;                /* Horizontal pen advance in pixels. */
} tsugumi_dawn_glyph;

/* Available when dawnView.font is set. Metrics and glyphs live for the view's
 * lifetime. Space and other blank glyphs have an empty atlas rectangle. */
const tsugumi_dawn_font_metrics* tsugumi_dawn_font_get_metrics(void);
/* Look up one Unicode scalar. Missing scalars return the U+FFFD glyph. */
const tsugumi_dawn_glyph* tsugumi_dawn_font_glyph(uint32_t codepoint);
/* Upload the embedded alpha coverage to an R8Unorm texture. Sample red as
 * coverage. The caller owns the returned texture and may cache it. */
WGPUTexture tsugumi_dawn_font_create_texture(WGPUDevice device,
                                             WGPUQueue queue);

#ifdef __cplusplus
}
#endif
