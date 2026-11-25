#include <tsugumi/dawn_helpers.h>

static WGPUDevice device;
static WGPUQueue queue;
static float rgb[3] = {0.15f, 0.15f, 0.15f};

int32_t tsugumi_view_abi(void) { return TSUGUMI_VIEW_ABI; }

int32_t tsugumi_view_start(WGPUDevice gpu, WGPUQueue commands,
                           WGPUTextureFormat format, uint32_t width,
                           uint32_t height, float scale) {
  (void)format;
  (void)width;
  (void)height;
  (void)scale;
  device = gpu;
  queue = commands;
  return 0;
}

int32_t tsugumi_view_resize(uint32_t width, uint32_t height, float scale,
                            tsugumi_view_time at) {
  (void)at;
  (void)width;
  (void)height;
  (void)scale;
  return 0;
}

int32_t tsugumi_view_message(uint32_t size, tsugumi_view_time at) {
  (void)at;
  if (size != sizeof(rgb)) return -1;
  return tsugumi_view_read((uint8_t*)rgb, size);
}

int32_t tsugumi_view_pointer(uint32_t kind, float x, float y, uint32_t buttons,
                             float wheel_x, float wheel_y, uint32_t modifiers,
                             tsugumi_view_time at) {
  (void)at;
  (void)buttons;
  (void)wheel_x;
  (void)wheel_y;
  (void)modifiers;
  if (kind == TSUGUMI_DAWN_POINTER_DOWN) {
    const float click[] = {x, y};
    // The process may already have finished. Leave the colour on the canvas.
    tsugumi_view_send("color", (const uint8_t*)click, sizeof(click));
  }
  return 0;
}

int32_t tsugumi_view_draw(WGPUTextureView target, tsugumi_view_time present) {
  (void)present;
  WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  color.view = target;
  color.loadOp = WGPULoadOp_Clear;
  color.storeOp = WGPUStoreOp_Store;
  color.clearValue = (WGPUColor){rgb[0], rgb[1], rgb[2], 1.0};
  WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  desc.colorAttachmentCount = 1;
  desc.colorAttachments = &color;
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
  WGPURenderPassEncoder pass =
      wgpuCommandEncoderBeginRenderPass(encoder, &desc);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
  WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
  wgpuQueueSubmit(queue, 1, &commands);
  wgpuCommandBufferRelease(commands);
  wgpuCommandEncoderRelease(encoder);
  return 0;  // Draw again after a message or an input event.
}
