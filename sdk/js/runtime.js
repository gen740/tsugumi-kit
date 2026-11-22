import { relative, dirname, isAbsolute, sep } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { Builder, ByteBuffer } from "flatbuffers";
import { CardInput } from "./tsugumi/card/card-input.js";
import { CardPort } from "./tsugumi/card/card-port.js";
import { CardResult, CardResultT } from "./tsugumi/card/card-result.js";
import { CardFailureT } from "./tsugumi/card/card-failure.js";
import { FailureKind } from "./tsugumi/data/failure-kind.js";
import { PortKind } from "./tsugumi/data/port-kind.js";
import { inputPorts, outputChecks, outputPorts, readParams } from "./bindings.js";
import { cancelled, remainingMs, send } from "./effects.js";
import { display } from "./display.js";
import { Failure } from "./failure.js";

const ctx = Object.freeze({ send, display, cancelled, remainingMs });

function pack(value) {
  const builder = new Builder();
  builder.finish(value.pack(builder));
  return builder.asUint8Array();
}

// Decode one FlatBuffer port value. A Merge yields its active member.
function readValue(type, bytes, name) {
  const table = type.read(new ByteBuffer(bytes));
  if (!type.members) return table.unpack();
  const member = type.members.find(({ tag }) => tag === table.valueType());
  if (!member) throw new Failure(FailureKind.invalid_request, `Invalid Merge input ${name}`);
  return readValue(member.type, table.value(new member.box()).bytesArray(), name);
}

function decode(bytes) {
  const envelope = CardInput.getRootAsCardInput(new ByteBuffer(bytes));
  const wire = new Map();
  for (let i = 0; i < envelope.inputsLength(); i++) {
    const port = envelope.inputs(i);
    wire.set(port.name(), port);
  }
  const ports = Object.create(null);
  for (const { name, kind, type } of inputPorts) {
    const port = wire.get(name);
    if (port === undefined) continue;
    ports[name] = kind === "Path" ? port.path()
      : readValue(type, port.bytesArray() ?? new Uint8Array(), name);
  }
  const need = name => {
    const value = ports[name];
    if (value !== undefined) return value;
    throw new Failure(FailureKind.input_missing, `Missing input ${name}`);
  };
  return { params: readParams(new ByteBuffer(envelope.paramsArray())).unpack(), ports, need };
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

function accepts(type, item) {
  return type.members ? type.members.some(member => accepts(member.type, item))
    : item instanceof type.object;
}

function typeNames(type) {
  return type.members ? type.members.flatMap(member => typeNames(member.type)) : [type.object.name];
}

// Pack an output value. A Merge boxes the member's bytes under its tag.
function packValue(type, item) {
  if (!type.members) {
    checkUnions(item, type.object);
    return pack(item);
  }
  const member = type.members.find(({ type }) => accepts(type, item));
  const box = new member.boxObject();
  box.bytes = Array.from(packValue(member.type, item));
  const merged = new type.object();
  merged.valueType = member.tag;
  merged.value = box;
  return pack(merged);
}

function encodePort(builder, name, kind, type, item) {
  const portName = builder.createString(name);
  if (kind === "Path") {
    if (typeof item !== "string" || item === "" || item.includes("\0")) {
      throw new Failure(FailureKind.output_invalid, `Output ${name} must be a path string`);
    }
    const path = builder.createString(item);
    return CardPort.createCardPort(builder, portName, PortKind.Path, 0, path);
  }
  if (!accepts(type, item)) {
    throw new Failure(FailureKind.output_invalid,
      `Output ${name} must be a ${typeNames(type).join(" or ")}`);
  }
  let bytes;
  try {
    bytes = packValue(type, item);
  } catch (error) {
    throw new Failure(FailureKind.output_invalid, `Invalid output ${name}: ${error}`);
  }
  const payload = CardPort.createBytesVector(builder, bytes);
  return CardPort.createCardPort(builder, portName, PortKind.FlatBuffer, payload, 0);
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
  for (const { name, kind, required, type } of outputPorts) {
    const item = Object.hasOwn(value, name) ? value[name] : undefined;
    if (item === undefined) {
      if (required) throw new Failure(FailureKind.output_invalid, `Missing output ${name}`);
      continue;
    }
    ports.push(encodePort(builder, name, kind, type, item));
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
      return pack(new CardResultT([], failure(error, entry)));
    }
  };
}
