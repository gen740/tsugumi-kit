import { HtmlRenderer, Parser } from './commonmark.cjs'
import { json, onData } from './data.js'

const parser = new Parser()
const renderer = new HtmlRenderer({ safe: true })

const article = document.createElement('article')
article.style.padding = '0 1em'
article.addEventListener('click', event => {
  if (event.target.closest('a')) event.preventDefault()
})
const root = onData(bytes => {
  article.innerHTML = renderer.render(parser.parse(json(bytes).text))
  if (!article.isConnected) root.append(article)
})
