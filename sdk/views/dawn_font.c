#include <tsugumi/dawn_font.h>

#include "font-metrics.h"

static const uint8_t atlas[] = {
#embed "font-atlas.bin"
};

const tsugumi_dawn_font_metrics* tsugumi_dawn_font_get_metrics(void) {
  return &metrics;
}

const tsugumi_dawn_glyph* tsugumi_dawn_font_glyph(uint32_t codepoint) {
  size_t first = 0;
  size_t end = sizeof(glyphs) / sizeof(*glyphs);
  while (first < end) {
    const size_t mid = first + (end - first) / 2;
    if (codepoint == glyphs[mid].codepoint) return &glyphs[mid].glyph;
    if (codepoint < glyphs[mid].codepoint)
      end = mid;
    else
      first = mid + 1;
  }
  return &glyphs[fallback_glyph].glyph;
}

WGPUTexture tsugumi_dawn_font_create_texture(WGPUDevice device,
                                             WGPUQueue queue) {
  const WGPUExtent3D extent = {metrics.atlas_width, metrics.atlas_height, 1};
  WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
  desc.dimension = WGPUTextureDimension_2D;
  desc.size = extent;
  desc.format = WGPUTextureFormat_R8Unorm;
  desc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
  const WGPUTexture texture = wgpuDeviceCreateTexture(device, &desc);
  if (!texture) return NULL;

  WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  destination.texture = texture;
  WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
  layout.bytesPerRow = metrics.atlas_width;
  layout.rowsPerImage = metrics.atlas_height;
  wgpuQueueWriteTexture(queue, &destination, atlas, sizeof(atlas), &layout,
                        &extent);
  return texture;
}
