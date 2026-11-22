import { onData } from './data.js'

export default function mount(root, data) {
  const text = document.createElement('pre')
  text.style.whiteSpace = 'pre-wrap'
  root.replaceChildren(text)
  onData(root, data, config => { text.textContent = config.text })
}
