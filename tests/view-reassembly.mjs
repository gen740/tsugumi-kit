import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'

let connect
const root = { querySelector: () => ({ remove() {} }) }
globalThis.document = { querySelector: () => root }
globalThis.window = {
  addEventListener: (_, listener) => { connect = listener },
  removeEventListener: () => { connect = undefined }
}
globalThis.MessagePort = class {}

const source = await readFile(process.argv[2], 'utf8')
const { onData } = await import('data:text/javascript,' + encodeURIComponent(source))
const rendered = []
onData(bytes => rendered.push([...new Uint8Array(bytes)]))
const port = new MessagePort()
connect({ data: { tsugumi: 1, port } })

function deliver(sequence, index, count, payload) {
  const bytes = new ArrayBuffer(16 + payload.length)
  const header = new DataView(bytes)
  for (const [word, value] of [0x4d475354, sequence, index, count].entries()) {
    header.setUint32(4 * word, value, true)
  }
  new Uint8Array(bytes, 16).set(payload)
  port.onmessage({ data: { type: 'data', bytes } })
}

port.onmessage({ data: { type: 'run', state: 'started' } })
assert.equal(root.dataset, undefined)
deliver(0, 0, 1, [42])
deliver(1, 0, 2, [1, 2])
assert.deepEqual(rendered, [[42]])
deliver(1, 0, 2, [1, 2])
deliver(1, 1, 2, [3])
deliver(2, 0, 2, [4])
deliver(3, 0, 1, [])
deliver(2, 1, 2, [5])
assert.deepEqual(rendered, [[42], [1, 2, 3], []])
