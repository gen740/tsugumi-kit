import { types } from "node:util";

// Registered by the embedder in the Card environment and its Node workers.
let binding;
function host() {
  return binding ??= process._linkedBinding("tsugumi_card");
}

export const ViewMode = Object.freeze({ PANEL: 0, WINDOW: 1, FULLSCREEN: 2 });

function requireId(id) {
  if (!types.isUint8Array(id) || id.byteLength !== 16) {
    throw new TypeError("Expected a 16-byte view ID");
  }
}

function isUint32(value) {
  return Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
}

export function viewOpen(view, mode = ViewMode.PANEL, width = 0, height = 0) {
  if (typeof view !== "string" || view.includes("\0")) {
    throw new TypeError("Expected a view name without NUL bytes");
  }
  if (![mode, width, height].every(isUint32)) {
    throw new RangeError("Expected an unsigned 32-bit mode and dimensions");
  }
  return host().viewOpen(view, mode, width, height);
}

export function routeRegister(session) {
  requireId(session);
  return host().routeRegister(session);
}

export function routeUnregister(route) {
  requireId(route);
  host().routeUnregister(route);
}

export function send(session, bytes) {
  requireId(session);
  if (!types.isUint8Array(bytes)) {
    throw new TypeError("Expected Uint8Array view data");
  }
  return host().send(session, bytes, false);
}

export function sendFramed(session, bytes) {
  requireId(session);
  return host().send(session, bytes, true);
}

export function cancelled() {
  return host().cancelled();
}

export function remainingMs() {
  return host().remainingMs();
}

export function recv(timeoutMs = -1) {
  if (!Number.isSafeInteger(timeoutMs)) {
    throw new TypeError("Expected a safe integer timeout");
  }
  return host().recv(timeoutMs);
}

export function artifactEffect(operation, value) {
  return host().artifact(operation, value);
}
