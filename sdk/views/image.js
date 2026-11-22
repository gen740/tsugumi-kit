import { json, onData } from './data.js'

const image = document.createElement('img')
image.style.cssText = 'display:block;width:100%;height:100%;object-fit:contain'
let url
const root = onData(bytes => {
  const size = new DataView(bytes).getUint32(0, true)
  const { media_type, alt } = json(new Uint8Array(bytes, 4, size))
  if (url) URL.revokeObjectURL(url)
  url = URL.createObjectURL(new Blob([new Uint8Array(bytes, 4 + size)], { type: media_type }))
  image.alt = alt
  image.src = url
  if (!image.isConnected) root.append(image)
})
