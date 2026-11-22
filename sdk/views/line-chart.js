// Named x/y series. The message is a u32 metadata length, the JSON
// {"series": [{"id", "label", "offset", "count"}]} and then every series'
// x values followed by every series' y values, as little-endian float64.
import { onData } from './data.js'
import { format, framed, palette, section, showError, surface, theme, ticks } from './chart.js'

function decode(bytes) {
  const { config, offset } = framed(bytes)
  if (!Array.isArray(config.series)) throw new Error('The series list is missing')
  let total = 0
  for (const row of config.series) {
    if (row.offset !== total || !Number.isSafeInteger(row.count) || row.count < 0) {
      throw new Error('The series ranges are not contiguous')
    }
    total += row.count
  }
  const x = section(bytes, offset, total, 'f64')
  const y = section(bytes, x.end, total, 'f64')
  if (y.end !== bytes.byteLength) throw new Error('The message has trailing bytes')
  return config.series.map((row, index) => ({
    label: String(row.label || row.id || `Series ${index + 1}`),
    color: palette[index % palette.length],
    x: x.values.subarray(row.offset, row.offset + row.count),
    y: y.values.subarray(row.offset, row.offset + row.count),
  }))
}

function extent(series, axis) {
  let min = Infinity, max = -Infinity
  for (const row of series) {
    for (const value of row[axis]) {
      if (value < min) min = value
      if (value > max) max = value
    }
  }
  if (min === Infinity) return [0, 1]
  return [min, max]
}

// Scaling before taking a difference also handles finite values near the double limits.
function axis(series, key, margin) {
  const [min, max] = extent(series, key)
  const scale = Math.max(Math.abs(min), Math.abs(max)) || 1
  let low = min / scale, high = max / scale
  const pad = low === high ? 0.05 : margin * (high - low)
  low = Math.max(low - pad, -Number.MAX_VALUE / scale)
  high = Math.min(high + pad, Number.MAX_VALUE / scale)
  return {
    ticks: target => ticks(low, high, target).map(value => ({
      position: (value - low) / (high - low),
      label: format(value * scale),
    })),
    position: value => (value / scale - low) / (high - low),
  }
}

let series = []
const legend = document.createElement('div')
legend.style.cssText = 'display:flex;flex-wrap:wrap;gap:4px 14px;padding:8px 12px 0;font-size:12px'

const { canvas, redraw } = surface(document.querySelector('main'), (context, width, height) => {
  const colors = theme()
  const xAxis = axis(series, 'x', 0)
  const yAxis = axis(series, 'y', 0.04)

  context.font = '11px system-ui, sans-serif'
  const yTicks = yAxis.ticks(Math.max(2, Math.floor(height / 60)))
  const left = 12 + Math.max(...yTicks.map(tick => context.measureText(tick.label).width))
  const right = width - 16, top = 12, bottom = height - 28
  if (right - left < 20 || bottom - top < 20) return
  const xTicks = xAxis.ticks(Math.max(2, Math.floor((right - left) / 90)))
  const px = position => left + position * (right - left)
  const py = position => bottom - position * (bottom - top)

  context.lineWidth = 1
  context.strokeStyle = colors.grid
  context.fillStyle = colors.muted
  context.textAlign = 'right'
  context.textBaseline = 'middle'
  for (const { position, label } of yTicks) {
    const y = Math.round(py(position)) + 0.5
    context.beginPath(); context.moveTo(left, y); context.lineTo(right, y); context.stroke()
    context.fillText(label, left - 6, y)
  }
  context.textAlign = 'center'
  context.textBaseline = 'top'
  for (const { position, label } of xTicks) {
    const x = Math.round(px(position)) + 0.5
    context.beginPath(); context.moveTo(x, top); context.lineTo(x, bottom); context.stroke()
    context.fillText(label, x, bottom + 6)
  }
  context.strokeStyle = colors.axis
  context.strokeRect(left + 0.5, top + 0.5, right - left, bottom - top)

  context.save()
  context.beginPath()
  context.rect(left, top, right - left, bottom - top)
  context.clip()
  context.lineWidth = 1.75
  context.lineJoin = 'round'
  for (const row of series) {
    context.strokeStyle = context.fillStyle = row.color
    if (row.x.length === 1) {
      context.beginPath()
      context.arc(px(xAxis.position(row.x[0])), py(yAxis.position(row.y[0])), 3, 0, 2 * Math.PI)
      context.fill()
      continue
    }
    context.beginPath()
    for (let i = 0; i < row.x.length; ++i) {
      const x = px(xAxis.position(row.x[i]))
      const y = py(yAxis.position(row.y[i]))
      if (i === 0) context.moveTo(x, y)
      else context.lineTo(x, y)
    }
    context.stroke()
  }
  context.restore()
})

canvas.setAttribute('aria-label', 'Line chart')

const root = onData(bytes => {
  try {
    series = decode(bytes)
  } catch (error) {
    showError(root, error)
    return
  }
  legend.replaceChildren(...series.map(row => {
    const item = document.createElement('span')
    const swatch = document.createElement('span')
    swatch.style.cssText = `display:inline-block;width:10px;height:3px;margin-right:6px;vertical-align:middle;background:${row.color}`
    item.append(swatch, `${row.label} (${row.x.length})`)
    return item
  }))
  if (!canvas.isConnected) root.replaceChildren(legend, canvas)
  redraw()
})
