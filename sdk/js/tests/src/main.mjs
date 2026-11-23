import assert from "node:assert/strict";
import { statSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { Worker } from "node:worker_threads";
import isNumber from "is-number";
import { Artifact, ByteBuffer, inputs, outputs, params, recv, send } from "@tsugumi/card";

export default function main(input, ctx) {
  const item = input.need("arrival");
  const opened = item.file.open();
  assert(statSync(opened).isDirectory());
  assert.equal(input.need("direct").open(), opened);
  const [member] = input.need("batch").items;
  assert.equal(member.schema, "4".repeat(64));
  const bytes = new ByteBuffer(Uint8Array.from(member.bytes));
  assert.equal(inputs.batch.Element.getRootAsElement(bytes).value(), 4);
  assert.equal(params.offset, 100n);
  assert(Object.isFrozen(params) && Object.isFrozen(params.labels));
  assert(params.flag && params.ratio === 1 && params.labels[0] === "日本語");
  assert(!ctx.cancelled() && isNumber(ctx.remainingMs()) && ctx.remainingMs() === 500);
  const number = typeof item.value === "bigint" ? item.value : 20n;
  if (number < 0n) return {};
  const [artifact, directory] = Artifact.dir();
  const file = join(directory, "value.txt");
  writeFileSync(file, "test");
  const upload = Artifact.upload(file);
  const [choice] = Artifact.dir();
  assert.deepEqual(recv("request"), { status: "message", bytes: Uint8Array.of(42) });
  assert.equal(recv("request", 0).status, "timeout");
  assert.equal(recv("request", 1).status, "cancelled");
  assert.equal(send("listing", Uint8Array.of(42)), 0);
  assert.equal(send("listing", new Uint8Array((1 << 24) + 5).fill(42)), 0);
  new Worker(new URL("./display-worker.mjs", import.meta.url));
  const child = new outputs.ChildT(artifact);
  return { listing: new outputs.ListingT(child, [upload, item.file],
    outputs.Choice.File, choice, params.offset + number) };
}
