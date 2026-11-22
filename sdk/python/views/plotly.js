import Plotly from './plotly.cjs'

export default async function mount(root, data) {
  const chart = document.createElement('div')
  root.replaceChildren(chart)

  let figure = JSON.parse(data.config)
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
    figure = JSON.parse(event.detail.config)
    void draw()
  })

  await draw()
  new ResizeObserver(() => { void draw() }).observe(root)
}
