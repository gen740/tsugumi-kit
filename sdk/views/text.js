import { json, onData } from './data.js'

const text = document.createElement('pre')
text.style.whiteSpace = 'pre-wrap'
const root = onData(bytes => {
  text.textContent = json(bytes).text
  if (!text.isConnected) root.append(text)
})
