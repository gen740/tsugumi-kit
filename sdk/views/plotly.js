import Plotly from './plotly.cjs'
import { json, onData } from './data.js'

const chart = document.createElement('div')
let figure
let pending = false
let drawing = false

async function draw() {
  pending = true
  if (drawing) return
  drawing = true
  try {
    while (pending) {
      pending = false
      await Plotly.react(chart, {
        ...figure,
        layout: {
          ...figure.layout, autosize: false,
          width: root.clientWidth, height: root.clientHeight,
        },
        config: { ...figure.config, responsive: false, displaylogo: false },
      })
    }
  } finally {
    drawing = false
  }
}

const root = onData(bytes => {
  figure = json(bytes)
  if (!chart.isConnected) root.append(chart)
  void draw()
})
new ResizeObserver(() => {
  if (figure) void draw()
}).observe(root)
