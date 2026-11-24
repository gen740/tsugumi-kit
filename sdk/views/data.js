const root = document.querySelector('main')
const empty = root.querySelector('[role="status"]')
const decoder = new TextDecoder()

export function json(bytes) {
  return JSON.parse(decoder.decode(bytes))
}

export function onData(render) {
  const pending = new Map()
  const update = bytes => {
    render(bytes)
    empty.remove()
  }
  tsugumi.onmessage = bytes => {
    if (!(bytes instanceof Uint8Array) ||
        bytes.byteLength < 20 || bytes.byteLength > (1 << 24)) return
    const header = new DataView(bytes.buffer, bytes.byteOffset, 20)
    if (header.getUint32(0, true) !== 0x4d475354) return
    const id = header.getBigUint64(4, true)
    const index = header.getUint32(12, true)
    const count = header.getUint32(16, true)
    if (!count || index >= count) return
    const payload = bytes.subarray(20)
    if (count === 1) {
      update(payload.slice().buffer)
      return
    }
    if (index === 0) {
      // A cancelled sender may never finish. Keep the most recent transfers.
      if (pending.size >= 16) pending.delete(pending.keys().next().value)
      pending.set(id, { count, chunks: [], length: 0 })
    }
    const message = pending.get(id)
    if (!message || message.count !== count || message.chunks.length !== index) return
    message.chunks.push(payload)
    message.length += payload.length
    pending.delete(id)
    if (message.chunks.length !== count) {
      pending.set(id, message)
      return
    }
    const assembled = new Uint8Array(message.length)
    let offset = 0
    for (const chunk of message.chunks) {
      assembled.set(chunk, offset)
      offset += chunk.length
    }
    update(assembled.buffer)
  }
  return root
}
