export function onData(root, data, render) {
  const draw = ({ config, blobs }) => render(JSON.parse(config), blobs)
  draw(data)
  root.addEventListener('tsugumi:data', event => draw(event.detail))
}
