const decoder = new TextDecoder()

// Call render(config, blobs) for the mounted blobs and for every update. The
// SDK encodes this module's config blob as UTF-8 JSON.
export function onData(root, blobs, render) {
  const draw = blobs => render(JSON.parse(decoder.decode(blobs.config)), blobs)
  draw(blobs)
  root.addEventListener('tsugumi:data', event => draw(event.detail))
}
