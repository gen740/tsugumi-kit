import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { writeFileSync } from "node:fs";
import { Worker } from "node:worker_threads";
import { setImmediate } from "node:timers/promises";
import { fileURLToPath } from "node:url";
import isNumber from "is-number";
import {
  outputs, Failure, FailureKind, Text, Markdown, Table, Image, Points, LineChart,
} from "@tsugumi/card";

let calls = 0;

/** @type {import("@tsugumi/card").Card} */
export default async function run({ ports, params }, ctx) {
  assert.equal(isNumber("42"), true);
  await setImmediate();
  const { display, send, cancelled, remainingMs } = ctx;
  const { file, directory } = ports;

  switch (params.mode) {
    case 0:
      calls++;
      break;
    case 1: {
      assert.equal(calls, 2);
      assert.equal(process.env.PATH, fileURLToPath(new URL("../../bin", import.meta.url)));
      assert.equal(execFileSync("sort", [], { input: "b\na\n", encoding: "utf8" }), "a\nb\n");
      assert.equal(cancelled(), false);
      assert.equal(remainingMs(), 500);
      assert.throws(() => send("bad\0name", new Uint8Array()), TypeError);
      assert.throws(() => send("custom", [42]), TypeError);
      const detached = new Uint8Array(1);
      detached.buffer.transfer();
      assert.throws(() => send("custom", detached), /detached buffer/);
      assert.throws(() => display.send(new Markdown(undefined)), TypeError);
      assert.throws(() => display.send(new Table(["a"], [[1, 2]])), RangeError);
      assert.throws(() => display.send(new LineChart([
        { id: "id", label: "line", x: [Infinity], y: [0] },
      ])), RangeError);
      const bytes = new Uint8Array([0, 13, 255]);
      assert.equal(display.send(new Text('résumé\n"')), 0);
      display.send(new Markdown("**text**"));
      display.send(new Table(["n"], [[1]]));
      display.send(new Image(bytes, "image/png", "sample"));
      display.send(new Points([1], [2], [3], [0x123456]));
      display.send(new LineChart([{ id: "id", label: "line", x: [2], y: [3] }]));
      assert.equal(send("custom", new Uint8Array([99, ...bytes, 99]).subarray(1, 4)), 0);
      const worker = new Worker(new URL("./display-worker.mjs", import.meta.url),
        { workerData: params.fd });
      worker.on("error", error => console.error(error));
      break;
    }
    case 3:
      throw new Failure(FailureKind.cancelled, "explicit failure", { cell: "fit" });
    case 4:
      throw new Error("standard exception");
    case 5:
      return Promise.reject("rejected value");
    case 6:
      return {};
    case 7:
      return { number: new outputs.number.NumberT() };
    case 9:
      return { number: new outputs.number.NumberT(
        new outputs.number.ValueT(42n, outputs.number.Detail.Code, new outputs.number.ValueT())) };
    case 10:
      return { number: new outputs.number.NumberT(
        new outputs.number.ValueT(42n, outputs.number.Detail.Code, null)) };
    case 8:
      assert.equal(process.env.PATH, undefined);
      assert.notEqual(send("custom", new Uint8Array()), 0);
      writeFileSync("result.txt", "42");
      return {
        number: new outputs.number.NumberT(new outputs.number.ValueT(42n)),
        file: new outputs.file.FileT("result.txt"),
      };
    case 11:
      assert.equal(send("undeclared", new Uint8Array([42])), 37);
      break;
  }
  const { x, y } = ports.number.position;
  return {
    number: new outputs.number.NumberT(
      new outputs.number.ValueT(x + y, outputs.number.Detail.Code, new outputs.number.CodeT(2n))),
    file: file && new outputs.file.FileT(file.value),
    directory: directory && new outputs.directory.DirectoryT(directory.value),
  };
}
