import { types } from "node:util";

// Registered by the embedder in the Card environment and its Node workers.
let binding;
function host() {
  return binding ??= process._linkedBinding("tsugumi_card");
}

export function send(view, bytes) {
  if (typeof view !== "string" || view.includes("\0")) {
    throw new TypeError("Expected a view name without NUL bytes");
  }
  if (!types.isUint8Array(bytes)) {
    throw new TypeError("Expected Uint8Array display data");
  }
  return host().send(view, bytes);
}

export function cancelled() {
  return host().cancelled();
}

export function remainingMs() {
  return host().remainingMs();
}

export function recv(view, timeoutMs = -1) {
  if (typeof view !== "string" || view.includes("\0") || !Number.isSafeInteger(timeoutMs)) {
    throw new TypeError("Expected a view name and a safe integer timeout");
  }
  return host().recv(view, timeoutMs);
}

export function artifactEffect(operation, value) {
  return host().artifact(operation, value);
}
