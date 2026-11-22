// Shared helpers for the canvas chart views.
import { json } from './data.js'

export const palette = ['#3b82f6', '#f97316', '#22c55e', '#e11d48', '#a855f7', '#0891b2', '#ca8a04', '#64748b']

// A u32 little-endian metadata length, UTF-8 JSON metadata, then binary sections.
export function framed(bytes) {
  if (bytes.byteLength < 4) throw new Error('The message is too short')
  const size = new DataView(bytes).getUint32(0, true)
  if (4 + size > bytes.byteLength) throw new Error('The metadata overruns the message')
  return { config: json(new Uint8Array(bytes, 4, size)), offset: 4 + size }
}

// Read count little-endian values of one type, starting at offset.
const readers = {
  f32: [4, Float32Array, (view, at) => view.getFloat32(at, true)],
  f64: [8, Float64Array, (view, at) => view.getFloat64(at, true)],
  u32: [4, Uint32Array, (view, at) => view.getUint32(at, true)],
}
export function section(bytes, offset, count, type) {
  const [size, Type, read] = readers[type]
  const end = offset + count * size
  if (!Number.isSafeInteger(end) || end > bytes.byteLength) throw new Error('A binary section overruns the message')
  const view = new DataView(bytes, offset, count * size)
  const values = new Type(count)
  for (let i = 0; i < count; ++i) values[i] = read(view, i * size)
  return { values, end }
}

export function theme() {
  const dark = matchMedia('(prefers-color-scheme: dark)').matches
  return dark
    ? { muted: '#8b919c', grid: '#2a2e36', axis: '#5b616c' }
    : { muted: '#68707c', grid: '#e7e9ed', axis: '#a3a9b3' }
}

// A canvas that fills the root and redraws at device resolution.
export function surface(root, draw) {
  root.style.cssText = 'height:100dvh;overflow:hidden;display:flex;flex-direction:column'
  const canvas = document.createElement('canvas')
  canvas.style.cssText = 'display:block;flex:1;min-height:0;width:100%;touch-action:none'
  let frame = 0
  const redraw = () => {
    if (frame) return
    frame = requestAnimationFrame(() => {
      frame = 0
      if (!canvas.isConnected) return
      const width = canvas.clientWidth, height = canvas.clientHeight
      const ratio = devicePixelRatio || 1
      canvas.width = Math.max(1, Math.round(width * ratio))
      canvas.height = Math.max(1, Math.round(height * ratio))
      const context = canvas.getContext('2d')
      context.setTransform(ratio, 0, 0, ratio, 0, 0)
      draw(context, width, height)
    })
  }
  new ResizeObserver(redraw).observe(canvas)
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', redraw)
  return { canvas, redraw }
}

export function showError(root, error) {
  const message = document.createElement('p')
  message.setAttribute('role', 'alert')
  message.style.cssText = 'margin:1em;color:#dc2626'
  message.textContent = `Cannot draw this message: ${error.message}`
  root.replaceChildren(message)
}

// Round tick steps of 1, 2 or 5 times a power of ten.
export function ticks(min, max, target) {
  if (!(max > min)) return [min]
  const raw = (max - min) / Math.max(1, target)
  const power = 10 ** Math.floor(Math.log10(raw))
  const step = [1, 2, 5, 10].map(m => m * power).find(s => s >= raw) ?? raw
  if (!Number.isFinite(step) || step === 0) return [min, max]
  const values = []
  const first = Math.ceil(min / step)
  for (let index = 0; index <= target + 1; index++) {
    const value = (first + index) * step
    if (value > max || values.at(-1) === value) break
    values.push(value === 0 ? 0 : value)
  }
  return values.length ? values : [min, max]
}

export function format(value) {
  const magnitude = Math.abs(value)
  if (magnitude !== 0 && (magnitude >= 1e6 || magnitude < 1e-3)) return value.toExponential(2)
  return String(Number(value.toPrecision(6)))
}
