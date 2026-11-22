import { FailureKind } from "./tsugumi/data/failure-kind.js";
import type { CardInput, CardOutput } from "./bindings.js";

export { inputs, outputs, parameters } from "./bindings.js";
export type { CardInput, CardOutput } from "./bindings.js";
export { FailureKind };

export class Failure extends Error {
  constructor(kind: FailureKind, message: string, options?: { cell?: string });
  readonly kind: FailureKind;
  readonly cell?: string;
}

export function send(view: string, bytes: Uint8Array): number;
export function cancelled(): boolean;
export function remainingMs(): number;

export class Text { constructor(text: string); }
export class Markdown extends Text {}
export class Table { constructor(columns: readonly string[], rows: readonly (readonly unknown[])[]); }
export class Image { constructor(data: Uint8Array, mediaType?: string, alt?: string); }

type Samples = readonly number[] | Float32Array | Float64Array;
export interface LineSeries {
  id: string;
  label: string;
  x: Samples;
  y: Samples;
}
export class LineChart { constructor(series: readonly LineSeries[]); }
export class Points {
  constructor(x: Samples, y: Samples, z: Samples, colors?: readonly number[] | Uint32Array);
}

export const display: Readonly<{
  send(value: string | Text | Markdown | Table | Image | LineChart | Points): number;
}>;

export interface CardContext {
  readonly send: typeof send;
  readonly display: typeof display;
  readonly cancelled: typeof cancelled;
  readonly remainingMs: typeof remainingMs;
}
export type Card = (input: CardInput, ctx: CardContext) => CardOutput | Promise<CardOutput>;
