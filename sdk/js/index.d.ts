import { FailureKind } from "./tsugumi/data/failure-kind.js";
import { ArtifactT } from "./tsugumi/artifact.js";
import type { CardInput, CardOutput, Params } from "./bindings.js";

export { inputs, outputs } from "./bindings.js";
export type { CardInput, CardOutput, CardPorts } from "./bindings.js";
export { FailureKind };
export const params: Params;

export class Artifact extends ArtifactT {
  /** keep defaults to true; false makes an Artifact that exists only while its run runs. */
  static dir(options?: { keep?: boolean }): [Artifact, string];
  static upload(path: string, options?: { keep?: boolean }): Artifact;
  open(): string;
}

declare module "./tsugumi/artifact.js" {
  interface ArtifactT {
    open(): string;
  }
}

export class Failure extends Error {
  constructor(kind: FailureKind, message: string, options?: { cell?: string });
  readonly kind: FailureKind;
  readonly cell?: string;
}

export const ViewMode: Readonly<{ PANEL: 0; WINDOW: 1; FULLSCREEN: 2 }>;
export type ViewMode = typeof ViewMode[keyof typeof ViewMode];
export type OpenedView = Readonly<{ route: Uint8Array; session: Uint8Array }>;
export type ViewEvent = Readonly<
  | { status: "message" | "close"; route: Uint8Array; session: Uint8Array; bytes: Uint8Array }
  | { status: "timeout" | "cancelled"; bytes: Uint8Array }
>;
export const ViewFlags: Readonly<{ NONE: 0; HIDE_CURSOR: 1; LOCK_POINTER: 2 }>;
export function viewOpen(view: string, mode?: ViewMode, flags?: number, width?: number, height?: number): OpenedView;
export function routeRegister(session: Uint8Array): Uint8Array;
export function routeUnregister(route: Uint8Array): void;
export function send(session: Uint8Array, bytes: Uint8Array): number;
export function recv(timeoutMs?: number): ViewEvent;
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
  recv: typeof recv;
  send(value: string | Text | Markdown | Table | Image | LineChart | Points, session?: Uint8Array): number;
}>;

export interface CardContext {
  readonly viewOpen: typeof viewOpen;
  readonly routeRegister: typeof routeRegister;
  readonly routeUnregister: typeof routeUnregister;
  readonly send: typeof send;
  readonly recv: typeof recv;
  readonly display: typeof display;
  readonly cancelled: typeof cancelled;
  readonly remainingMs: typeof remainingMs;
}
export type Card = (input: CardInput, ctx: CardContext) => CardOutput | Promise<CardOutput>;
export { ByteBuffer } from "flatbuffers";
