import { types } from "node:util";
import { send as sendRaw } from "./effects.js";

export class Text {
  constructor(text) { this.text = text; }
}

export class Markdown extends Text {}

export class Table {
  constructor(columns, rows) {
    this.columns = columns;
    this.rows = rows;
  }
}

export class Image {
  constructor(data, mediaType = "image/png", alt = "") {
    this.data = data;
    this.mediaType = mediaType;
    this.alt = alt;
  }
}

export class LineChart {
  constructor(series) { this.series = series; }
}

export class Points {
  constructor(x, y, z, colors = []) {
    this.x = x;
    this.y = y;
    this.z = z;
    this.colors = colors;
  }
}

const encoder = new TextEncoder();

// The HTML views define this private metadata. Card input and output use FlatBuffers.
function metadata(value) {
  return encoder.encode(JSON.stringify(value));
}

function frame(config, byteLength, write) {
  const header = metadata(config);
  const bytes = new Uint8Array(4 + header.length + byteLength);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, header.length, true);
  bytes.set(header, 4);
  write(bytes, view, 4 + header.length);
  return bytes;
}

function writeAxis(view, offset, values, setter, width) {
  for (const value of values) {
    view[setter](offset, value, true);
    offset += width;
  }
  return offset;
}

function points({ x, y, z, colors }) {
  const count = x.length;
  if (y.length !== count || z.length !== count ||
      (colors.length && colors.length !== count)) {
    throw new RangeError("Points require matching x, y, z and optional RGB colors");
  }
  return frame({ count }, 4 * (3 * count + colors.length), (_, view, offset) => {
    for (const axis of [x, y, z]) {
      offset = writeAxis(view, offset, axis, "setFloat32", 4);
    }
    writeAxis(view, offset, colors, "setUint32", 4);
  });
}

function lineChart({ series }) {
  let total = 0;
  const rows = series.map(({ id, label, x, y }) => {
    if (typeof id !== "string" || typeof label !== "string") {
      throw new TypeError("Line chart IDs and labels must be strings");
    }
    const count = x.length;
    if (y.length !== count) throw new RangeError("Line chart axes must match");
    for (const axis of [x, y]) {
      for (const value of axis) {
        if (!Number.isFinite(value)) throw new RangeError("Line chart values must be finite");
      }
    }
    const row = { count, id, label, offset: total };
    total += count;
    return row;
  });
  return frame({ series: rows }, 16 * total, (_, view, offset) => {
    for (const axis of ["x", "y"]) {
      for (const row of series) {
        offset = writeAxis(view, offset, row[axis], "setFloat64", 8);
      }
    }
  });
}

function send(value) {
  if (value instanceof Text || typeof value === "string") {
    const text = typeof value === "string" ? value : value.text;
    if (typeof text !== "string") throw new TypeError("Expected display text to be a string");
    return sendRaw(value instanceof Markdown ? "markdown" : "text", metadata({ text }));
  }
  if (value instanceof Table) {
    const columns = Array.from(value.columns, String);
    const rows = Array.from(value.rows, row => Array.from(row, String));
    if (rows.some(row => row.length !== columns.length)) {
      throw new RangeError("Display table rows must match the column count");
    }
    return sendRaw("table", metadata({ columns, rows }));
  }
  if (value instanceof Image) {
    if (!types.isUint8Array(value.data)) throw new TypeError("Expected Uint8Array image data");
    if (typeof value.mediaType !== "string" || typeof value.alt !== "string") {
      throw new TypeError("Image media type and alt text must be strings");
    }
    const bytes = frame({ alt: value.alt, media_type: value.mediaType },
      value.data.length, (bytes, _, offset) => bytes.set(value.data, offset));
    return sendRaw("image", bytes);
  }
  if (value instanceof LineChart) return sendRaw("line-chart", lineChart(value));
  if (value instanceof Points) return sendRaw("points", points(value));
  throw new TypeError("Unsupported display value");
}

export const display = Object.freeze({ send });
