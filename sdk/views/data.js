const root = document.querySelector('main')
const empty = root.querySelector('[role="status"]')
const decoder = new TextDecoder()

export function json(bytes) {
  return JSON.parse(decoder.decode(bytes))
}

export function onData(render) {
  const connect = event => {
    if (event.data?.tsugumi !== 1 ||
        !(event.data.port instanceof MessagePort)) return

    window.removeEventListener('message', connect)
    event.data.port.onmessage = ({ data }) => {
      if (data?.type === 'run' && (data.state === 'started' || data.state === 'ended')) {
        root.dataset.run = data.state
      }
      if (data?.type === 'data' && data.bytes instanceof ArrayBuffer) {
        render(data.bytes)
        empty.remove()
      }
    }
  }
  window.addEventListener('message', connect)
  return root
}
