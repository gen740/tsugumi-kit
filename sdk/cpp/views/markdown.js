import { HtmlRenderer, Parser } from './commonmark.cjs'
import { onData } from './data.js'

const parser = new Parser()
const renderer = new HtmlRenderer({ safe: true })

export default function mount(root, data) {
  const article = document.createElement('article')
  article.style.padding = '0 1em'
  article.addEventListener('click', event => {
    if (event.target.closest('a')) event.preventDefault()
  })
  root.replaceChildren(article)
  onData(root, data, config => {
    article.innerHTML = renderer.render(parser.parse(config.text))
  })
}
