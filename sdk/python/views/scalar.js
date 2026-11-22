import { onData } from './data.js'

export default function mount(root, data) {
  const scalar = document.createElement('div')
  scalar.style.cssText = 'display:grid;place-content:center;min-height:100%;text-align:center'
  const value = document.createElement('div')
  value.style.cssText = 'font-size:clamp(24px,9vmin,64px);font-variant-numeric:tabular-nums'
  const label = document.createElement('div')
  scalar.append(value, label)
  root.replaceChildren(scalar)
  onData(root, data, config => {
    value.textContent = config.value ?? '—'
    label.textContent = config.label
  })
}
