const root = document.querySelector('main')
const empty = root.querySelector('[role="status"]')
const decoder = new TextDecoder()

export function json(bytes) {
  return JSON.parse(decoder.decode(bytes))
}

export function onData(render) {
  let pending
  const connect = event => {
    if (event.data?.tsugumi !== 1 ||
        !(event.data.port instanceof MessagePort)) return

    window.removeEventListener('message', connect)
    event.data.port.onmessage = ({ data }) => {
      if (data?.type !== 'data' || !(data.bytes instanceof ArrayBuffer)) return
      const bytes = data.bytes
      if (bytes.byteLength < 16 || bytes.byteLength > (1 << 24)) return
      const header = new DataView(bytes, 0, 16)
      if (header.getUint32(0, true) !== 0x4d475354) return
      const sequence = header.getUint32(4, true)
      const index = header.getUint32(8, true)
      const count = header.getUint32(12, true)
      if (!count || index >= count) return
      const payload = new Uint8Array(bytes, 16)
      if (index === 0) pending = { sequence, count, chunks: [], length: 0 }
      if (!pending || pending.sequence !== sequence || pending.count !== count ||
          pending.chunks.length !== index) return
      pending.chunks.push(payload)
      pending.length += payload.length
      if (pending.chunks.length !== count) return

      const message = pending
      pending = undefined
      const assembled = new Uint8Array(message.length)
      let offset = 0
      for (const chunk of message.chunks) {
        assembled.set(chunk, offset)
        offset += chunk.length
      }
      render(assembled.buffer)
      empty.remove()
    }
  }
  window.addEventListener('message', connect)
  return root
}
