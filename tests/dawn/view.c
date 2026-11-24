#include <tsugumi/dawn_font.h>
#include <tsugumi/dawn_helpers.h>

static const tsugumi_view_time epoch = UINT64_C(1) << 33;
static const tsugumi_view_text_area text_area = {12.0f, 8.0f, 200.0f, 24.0f};
static float stereo[2];

int32_t tsugumi_view_abi(void) { return TSUGUMI_VIEW_ABI; }

int32_t tsugumi_view_start(WGPUDevice device, WGPUQueue queue,
                           WGPUTextureFormat format, uint32_t width,
                           uint32_t height, float scale) {
  (void)format;
  (void)width;
  (void)height;
  (void)scale;

  const tsugumi_dawn_font_metrics* metrics = tsugumi_dawn_font_get_metrics();
  const tsugumi_dawn_glyph* fallback = tsugumi_dawn_font_glyph(0xFFFD);
  const tsugumi_dawn_glyph* space = tsugumi_dawn_font_glyph(' ');
  if (metrics->pixel_size != 24 || metrics->ascent <= 0 ||
      metrics->descent <= 0 || space == fallback || space->width ||
      space->height || space->advance <= 0 ||
      tsugumi_dawn_font_glyph(0x10FFFF) != fallback)
    return -1;

  const uint32_t examples[] = {'A', 0x65E5, 0x672C, 0x8A9E, 0x3042, 0x30AB};
  for (size_t i = 0; i < sizeof(examples) / sizeof(*examples); ++i) {
    const tsugumi_dawn_glyph* glyph = tsugumi_dawn_font_glyph(examples[i]);
    if (glyph == fallback || !glyph->width || !glyph->height ||
        glyph->advance <= 0 ||
        glyph->x + glyph->width >= metrics->atlas_width ||
        glyph->y + glyph->height >= metrics->atlas_height)
      return -1;
  }

  const WGPUTexture atlas = tsugumi_dawn_font_create_texture(device, queue);
  if (!atlas) return -1;
  wgpuTextureRelease(atlas);
  return 0;
}

int32_t tsugumi_view_resize(uint32_t width, uint32_t height, float scale,
                            tsugumi_view_time at) {
  return width == 640 && height == 480 && scale == 2.0f && at == epoch + 1 ? 0
                                                                           : -1;
}

int32_t tsugumi_view_draw(WGPUTextureView target, tsugumi_view_time present) {
  (void)target;
  return present == epoch + 2 && tsugumi_dawn_seconds(48000) == 1.0
             ? TSUGUMI_DAWN_DRAW_AGAIN
             : -1;
}

int32_t tsugumi_view_message(uint32_t size, tsugumi_view_time at) {
  uint8_t byte;
  if (size != 1 || at != epoch + 3 || tsugumi_view_read(&byte, size) != 0)
    return -1;
  return tsugumi_dawn_send_to_opener(&byte, size);
}

const float* tsugumi_view_audio(uint32_t frames, tsugumi_view_time heard) {
  if (frames != 1 || heard != epoch + 4) return NULL;
  stereo[0] = 0.25f;
  stereo[1] = -0.25f;
  return stereo;
}

int32_t tsugumi_view_pointer(uint32_t kind, float x, float y, uint32_t buttons,
                             float wheel_x, float wheel_y, uint32_t modifiers,
                             tsugumi_view_time at) {
  return kind == TSUGUMI_DAWN_POINTER_MOVE && x == -4 && y == 6 &&
                 buttons == TSUGUMI_DAWN_BUTTON_PRIMARY && wheel_x == 0 &&
                 wheel_y == 0 && modifiers == TSUGUMI_DAWN_MOD_SHIFT &&
                 at == epoch + 5
             ? 0
             : -1;
}

int32_t tsugumi_view_key(uint32_t kind, uint32_t code, uint32_t modifiers,
                         tsugumi_view_time at) {
  return kind == TSUGUMI_DAWN_KEY_DOWN && code == 4 && modifiers == 0 &&
                 at == epoch + 6
             ? 0
             : -1;
}

const tsugumi_view_text_area* tsugumi_view_text_input(void) {
  return &text_area;
}

static int32_t read_japanese(uint32_t size) {
  uint8_t bytes[3];
  if (size != sizeof(bytes) || tsugumi_view_read(bytes, size) != 0) return -1;
  return bytes[0] == 0xE6 && bytes[1] == 0x97 && bytes[2] == 0xA5 ? 0 : -1;
}

int32_t tsugumi_view_text(uint32_t size, tsugumi_view_time at) {
  return at == epoch + 7 ? read_japanese(size) : -1;
}

int32_t tsugumi_view_composition(uint32_t size, uint32_t cursor,
                                 tsugumi_view_time at) {
  return at == epoch + 8 && cursor == 3 ? read_japanese(size) : -1;
}

int32_t tsugumi_view_gamepad(uint32_t pad, uint32_t kind, uint32_t buttons,
                             float lx, float ly, float rx, float ry, float lt,
                             float rt, tsugumi_view_time at) {
  return pad == 0 && kind == TSUGUMI_DAWN_GAMEPAD_CHANGED && buttons == 1 &&
                 lx == -1 && ly == 1 && rx == 0 && ry == 0 && lt == 0 &&
                 rt == 1 && at == epoch + 9
             ? 0
             : -1;
}
