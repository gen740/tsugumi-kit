import { onData } from './data.js'

export default function mount(root, blobs) {
  const image = document.createElement('img')
  image.style.cssText = 'display:block;width:100%;height:100%;object-fit:contain'
  root.replaceChildren(image)
  let url
  onData(root, blobs, (config, blobs) => {
    if (url) URL.revokeObjectURL(url)
    url = URL.createObjectURL(new Blob([blobs.image], { type: config.media_type }))
    image.alt = config.alt
    image.src = url
  })
}
