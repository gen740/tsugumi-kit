import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'

const root = { querySelector: () => ({ remove() {} }) }
globalThis.document = { querySelector: () => root }
globalThis.tsugumi = {}

const source = await readFile(process.argv[2], 'utf8')
const { onData } = await import('data:text/javascript,' + encodeURIComponent(source))
const rendered = []
assert.equal(onData(bytes => rendered.push([...new Uint8Array(bytes)])), root)

function deliver(id, index, count, payload, offset = 0) {
  const length = 20 + payload.length
  const bytes = new Uint8Array(new ArrayBuffer(offset + length + 1), offset, length)
  const header = new DataView(bytes.buffer, bytes.byteOffset, 20)
  header.setUint32(0, 0x4d475354, true)
  header.setBigUint64(4, BigInt(id), true)
  header.setUint32(12, index, true)
  header.setUint32(16, count, true)
  bytes.set(payload, 20)
  tsugumi.onmessage(bytes)
}

tsugumi.onmessage(new Uint8Array(19))
tsugumi.onmessage(new Uint8Array(20))
deliver(0, 1, 1, [99])
deliver(0, 0, 0, [99])
assert.deepEqual(rendered, [])

deliver(0, 0, 1, [42])
deliver(1, 0, 2, [1, 2])
assert.deepEqual(rendered, [[42]])
deliver(2, 1, 2, [99])
deliver(1, 1, 2, [3])

deliver(2, 0, 2, [4], 3)
deliver(5, 0, 2, [8])
deliver(2, 0, 1, [5], 1)
deliver(3, 0, 1, [])
assert.deepEqual(rendered, [[42], [1, 2, 3], [5], []])
deliver(2, 1, 2, [6], 2)
deliver(5, 1, 2, [9])
deliver(4, 0, 1, [7])
assert.deepEqual(rendered, [[42], [1, 2, 3], [5], [], [4, 6], [8, 9], [7]])
