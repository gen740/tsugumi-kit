import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const module = await WebAssembly.compile(await readFile(process.argv[2]));
let memory;
let incoming;
let sent;
let uploaded = false;
let released = false;

function bytes(pointer, size) {
  return new Uint8Array(memory.buffer, pointer, size);
}

const { exports: view } = await WebAssembly.instantiate(module, {
  webgpu: {
    wgpuDeviceCreateTexture(device, descriptor) {
      assert.equal(device, 1);
      assert.ok(descriptor);
      return 3;
    },
    wgpuQueueWriteTexture(queue, destination, pixels, size, layout, extent) {
      assert.equal(queue, 2);
      assert.ok(destination);
      assert.ok(layout);
      const data = new DataView(memory.buffer);
      const width = data.getUint32(extent, true);
      const height = data.getUint32(extent + 4, true);
      assert.equal(data.getUint32(extent + 8, true), 1);
      assert.ok(width > 0 && width % 256 === 0);
      assert.ok(height > 0 && height <= 4096);
      assert.equal(size, width * height);
      const coverage = bytes(pixels, size);
      assert.equal(coverage[0], 0);
      assert.ok(coverage.some(value => value > 0 && value < 255));
      uploaded = true;
    },
    wgpuTextureRelease(texture) {
      assert.equal(texture, 3);
      assert.ok(uploaded);
      released = true;
    },
  },
  tsugumi: {
    tsugumi_view_read(pointer, size) {
      assert.equal(size, incoming.length);
      bytes(pointer, size).set(incoming);
      return 0;
    },
    tsugumi_view_send(route, pointer, size) {
      assert.deepEqual(bytes(route, 16), new Uint8Array(16));
      sent = bytes(pointer, size).slice();
      return 0;
    },
  },
});
memory = view.memory;
const epoch = 1n << 33n;
assert.equal(view.tsugumi_view_abi(), 0);
assert.equal(view.tsugumi_view_start(1, 2, 0, 320, 240, 1), 0);
assert.ok(released);
assert.equal(view.tsugumi_view_resize(640, 480, 2, epoch + 1n), 0);
assert.equal(view.tsugumi_view_draw(3, epoch + 2n), 1);
incoming = Uint8Array.of(42);
assert.equal(view.tsugumi_view_message(incoming.length, epoch + 3n), 0);
assert.deepEqual(sent, incoming);
assert.equal(view.tsugumi_view_audio(2, epoch + 4n), 0);
const audio = view.tsugumi_view_audio(1, epoch + 4n);
assert.ok(audio);
assert.deepEqual([...new Float32Array(memory.buffer, audio, 2)], [0.25, -0.25]);
assert.equal(view.tsugumi_view_pointer(0, -4, 6, 1, 0, 0, 1, epoch + 5n), 0);
assert.equal(view.tsugumi_view_key(0, 4, 0, epoch + 6n), 0);
const area = view.tsugumi_view_text_input();
assert.deepEqual([...new Float32Array(memory.buffer, area, 4)], [12, 8, 200, 24]);
incoming = new TextEncoder().encode("日");
assert.equal(view.tsugumi_view_text(incoming.length, epoch + 7n), 0);
assert.equal(view.tsugumi_view_composition(incoming.length, 3, epoch + 8n), 0);
assert.equal(view.tsugumi_view_gamepad(0, 1, 1, -1, 1, 0, 0, 0, 1, epoch + 9n), 0);
