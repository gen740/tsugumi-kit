import { onData } from './data.js'

export default function mount(root, blobs) {
  const text = document.createElement('pre')
  text.style.whiteSpace = 'pre-wrap'
  root.replaceChildren(text)
  onData(root, blobs, config => { text.textContent = config.text })
}
