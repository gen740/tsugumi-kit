import Plotly from './plotly.cjs'

const decoder = new TextDecoder()
const parse = blobs => JSON.parse(decoder.decode(blobs.config))

export default async function mount(root, blobs) {
  const chart = document.createElement('div')
  root.replaceChildren(chart)

  let figure = parse(blobs)
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

  root.addEventListener('tsugumi:data', event => {
    figure = parse(event.detail)
    void draw()
  })

  await draw()
  new ResizeObserver(() => { void draw() }).observe(root)
}
