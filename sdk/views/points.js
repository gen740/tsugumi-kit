import { onData } from './data.js'
import { framed, palette, section, showError, surface, theme } from './chart.js'

function decode(bytes) {
  const { config: { count }, offset } = framed(bytes)
  if (!Number.isSafeInteger(count) || count < 0) throw new Error('Invalid point count')
  const x = section(bytes, offset, count, 'f32')
  const y = section(bytes, x.end, count, 'f32')
  const z = section(bytes, y.end, count, 'f32')
  const colors = section(bytes, z.end, bytes.byteLength === z.end ? 0 : count, 'u32')
  if (colors.end !== bytes.byteLength) throw new Error('Invalid color section')

  const points = []
  const min = [Infinity, Infinity, Infinity]
  const max = [-Infinity, -Infinity, -Infinity]
  for (let index = 0; index < count; index++) {
    const position = [x.values[index], y.values[index], z.values[index]]
    if (!position.every(Number.isFinite)) continue
    const color = colors.values[index]
    points.push({ position, color: color === undefined
      ? palette[0] : '#' + (color & 0xffffff).toString(16).padStart(6, '0') })
    position.forEach((value, axis) => {
      min[axis] = Math.min(min[axis], value)
      max[axis] = Math.max(max[axis], value)
    })
  }
  if (points.length === 0) return points
  const center = min.map((value, axis) => (value + max[axis]) / 2)
  const span = Math.max(...min.map((value, axis) => max[axis] - value)) || 1
  for (const point of points) {
    point.position = point.position.map((value, axis) => (value - center[axis]) / span)
  }
  return points
}

let points = []
let yaw = -0.6
let pitch = 0.45
const { canvas, redraw } = surface(document.querySelector('main'), (context, width, height) => {
  const scale = Math.min(width, height) * 0.53
  const cy = Math.cos(yaw), sy = Math.sin(yaw)
  const cp = Math.cos(pitch), sp = Math.sin(pitch)
  const project = ([x, y, z]) => {
    const lateral = cy * x + sy * y
    const forward = cy * y - sy * x
    return {
      x: width / 2 + scale * lateral,
      y: height / 2 - scale * (cp * z - sp * forward),
      depth: cp * forward + sp * z,
    }
  }
  const origin = project([0, 0, 0])
  context.font = '12px system-ui, sans-serif'
  context.strokeStyle = theme().axis
  context.fillStyle = theme().muted
  for (const [axis, position] of Object.entries({ x: [0.94, 0, 0], y: [0, 0.94, 0], z: [0, 0, 0.94] })) {
    const to = project(position)
    context.beginPath()
    context.moveTo(origin.x, origin.y)
    context.lineTo(to.x, to.y)
    context.stroke()
    context.fillText(axis, to.x + 4, to.y - 4)
  }
  const projected = points.map(point => ({ ...project(point.position), color: point.color }))
  projected.sort((a, b) => b.depth - a.depth)
  const radius = Math.max(1.25, Math.min(4, scale / Math.sqrt(points.length) / 4))
  for (const point of projected) {
    context.fillStyle = point.color
    context.beginPath()
    context.arc(point.x, point.y, radius, 0, 2 * Math.PI)
    context.fill()
  }
})

canvas.tabIndex = 0
canvas.setAttribute('aria-label', '3D point cloud. Drag or use the arrow keys to rotate.')
canvas.style.cursor = 'grab'

function rotate(dx, dy) {
  yaw += dx
  pitch = Math.max(-Math.PI / 2, Math.min(Math.PI / 2, pitch + dy))
  redraw()
}

canvas.addEventListener('pointerdown', event => {
  if (event.button !== 0) return
  canvas.setPointerCapture(event.pointerId)
  canvas.style.cursor = 'grabbing'
})
canvas.addEventListener('pointermove', event => {
  if (canvas.hasPointerCapture(event.pointerId)) rotate(event.movementX * 0.01, event.movementY * 0.01)
})
canvas.addEventListener('lostpointercapture', () => { canvas.style.cursor = 'grab' })
canvas.addEventListener('keydown', event => {
  const delta = { ArrowLeft: [-0.1, 0], ArrowRight: [0.1, 0], ArrowUp: [0, -0.1], ArrowDown: [0, 0.1] }[event.key]
  if (!delta) return
  event.preventDefault()
  rotate(...delta)
})

const root = onData(bytes => {
  try {
    points = decode(bytes)
  } catch (error) {
    showError(root, error)
    return
  }
  if (!canvas.isConnected) root.replaceChildren(canvas)
  redraw()
})
