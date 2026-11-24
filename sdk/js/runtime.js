import { relative, dirname, isAbsolute, sep } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { Builder, ByteBuffer } from "flatbuffers";
import { CardInput } from "./tsugumi/card/card-input.js";
import { CardPort } from "./tsugumi/card/card-port.js";
import { CardResult } from "./tsugumi/card/card-result.js";
import { CardFailureT } from "./tsugumi/card/card-failure.js";
import { FailureKind } from "./tsugumi/data/failure-kind.js";
import { inputPorts, outputChecks, outputPorts } from "./bindings.js";
import { cancelled, remainingMs, recv, routeRegister, routeUnregister, send, viewOpen } from "./effects.js";
import "./artifact.js";
import { display } from "./display.js";
import { Failure } from "./failure.js";

const ctx = Object.freeze({ viewOpen, routeRegister, routeUnregister, send, recv, display, cancelled, remainingMs });

function pack(value) {
  const builder = new Builder();
  builder.finish(value.pack(builder));
  return builder.asUint8Array();
}

function decode(bytes) {
  const envelope = CardInput.getRootAsCardInput(new ByteBuffer(bytes));
  const wire = new Map();
  for (let i = 0; i < envelope.inputsLength(); i++) {
    const port = envelope.inputs(i);
    wire.set(port.name(), port);
  }
  const ports = Object.create(null);
  for (const { name, candidates } of inputPorts) {
    const port = wire.get(name);
    const candidate = candidates.get(port.schema());
    ports[name] = candidate.read(new ByteBuffer(port.bytesArray())).unpack();
  }
  const need = name => {
    const value = ports[name];
    if (value !== undefined) return value;
    throw new Failure(FailureKind.input_missing, `Missing input ${name}`);
  };
  return { ports, need };
}

function checkUnion(item, tag, variants, required, member) {
  if (tag === 0 && item == null && !required) return;
  const type = Number.isInteger(tag) && Object.hasOwn(variants, tag) ? variants[tag] : null;
  if (type === String ? typeof item === "string" || item instanceof Uint8Array
      : type && item instanceof type) {
    checkUnions(item, type);
    return;
  }
  throw new TypeError(`${member} does not match its union tag`);
}

function checkUnions(value, object) {
  const rules = outputChecks.get(object);
  if (!rules) return;
  for (const [member, tag, vector, required, variants] of rules.unions) {
    const item = value[member];
    const tags = value[tag];
    if (!vector) {
      checkUnion(item, tags, variants, required, member);
      continue;
    }
    if (item.length !== tags.length) throw new TypeError(`${member} has mismatched union vectors`);
    for (let i = 0; i < item.length; i++) {
      checkUnion(item[i], tags[i], variants, true, member);
    }
  }
  for (const [member, vector, child] of rules.children) {
    const valueAtMember = value[member];
    if (valueAtMember == null) continue;
    if (vector) {
      for (const item of valueAtMember) checkUnions(item, child);
    } else {
      checkUnions(valueAtMember, child);
    }
  }
}

function encodePort(builder, name, type, item) {
  if (!(item instanceof type.object)) {
    throw new Failure(FailureKind.output_invalid, `Output ${name} must be a ${type.object.name}`);
  }
  let bytes;
  try {
    checkUnions(item, type.object);
    bytes = pack(item);
  } catch (error) {
    throw new Failure(FailureKind.output_invalid, `Invalid output ${name}: ${error}`);
  }
  const portName = builder.createString(name);
  const payload = CardPort.createBytesVector(builder, bytes);
  CardPort.startCardPort(builder);
  CardPort.addName(builder, portName);
  CardPort.addBytes(builder, payload);
  return CardPort.endCardPort(builder);
}

function encode(value) {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new Failure(FailureKind.output_invalid, "Card must return an output object");
  }
  if (Object.keys(value).some(name => !outputPorts.some(port => port.name === name))) {
    throw new Failure(FailureKind.output_invalid, "Unknown output port");
  }
  const builder = new Builder();
  const ports = [];
  for (const { name, required, type } of outputPorts) {
    const item = Object.hasOwn(value, name) ? value[name] : undefined;
    if (item === undefined) {
      if (required) throw new Failure(FailureKind.output_invalid, `Missing output ${name}`);
      continue;
    }
    ports.push(encodePort(builder, name, type, item));
  }
  const outputs = CardResult.createOutputsVector(builder, ports);
  CardResult.startCardResult(builder);
  CardResult.addOutputs(builder, outputs);
  builder.finish(CardResult.endCardResult(builder));
  return builder.asUint8Array();
}

function failure(error, entry) {
  try {
    const structured = error instanceof Failure;
    const message = structured ? String(error.message) : String(error);
    const cell = structured && typeof error.cell === "string" ? error.cell : null;
    const kind = structured && typeof error.kind === "number" &&
        error.kind !== FailureKind.unspecified && Object.hasOwn(FailureKind, error.kind)
      ? error.kind : FailureKind.worker_failed;
    // Match file URLs in Node stack frames and keep paths inside the Card source.
    const root = dirname(entry);
    for (const match of String(error?.stack ?? "").matchAll(/(file:\/\/[^\s)]+):(\d+):\d+/g)) {
      const path = relative(root, fileURLToPath(match[1]));
      if (path && path !== ".." && !path.startsWith(`..${sep}`) &&
          !isAbsolute(path) && !path.startsWith(`node_modules${sep}`)) {
        return new CardFailureT(kind, message, `src/${path}`, Number(match[2]), cell);
      }
    }
    return new CardFailureT(kind, message, null, 0, cell);
  } catch {
    return new CardFailureT(FailureKind.worker_failed, "Cannot format JavaScript failure");
  }
}

export function bind(entry) {
  const load = import(pathToFileURL(entry).href).then(module => {
    if (typeof module.default !== "function") throw new TypeError("Card must export a default function");
    return module.default;
  });
  return async bytes => {
    try {
      const run = await load;
      return encode(await run(decode(bytes), ctx));
    } catch (error) {
      const builder = new Builder();
      const report = failure(error, entry).pack(builder);
      CardResult.startCardResult(builder);
      CardResult.addFailure(builder, report);
      builder.finish(CardResult.endCardResult(builder));
      return builder.asUint8Array();
    }
  };
}
