import { json, onData } from './data.js'

const table = document.createElement('table')
table.style.cssText = 'border-spacing:12px 4px;white-space:pre-wrap;text-align:left'
const root = onData(bytes => {
  const { columns, rows } = json(bytes)
  table.replaceChildren()
  const header = table.createTHead().insertRow()
  for (const name of columns) {
    const cell = document.createElement('th')
    cell.scope = 'col'
    cell.textContent = name
    header.append(cell)
  }
  const body = table.createTBody()
  for (const values of rows) {
    const row = body.insertRow()
    for (const value of values) row.insertCell().textContent = value
  }
  if (!table.isConnected) root.append(table)
})
