import assert from "node:assert/strict";
import { readSync } from "node:fs";
import { workerData } from "node:worker_threads";
import { cancelled, display, remainingMs } from "@tsugumi/card";

assert.equal(readSync(workerData, Buffer.alloc(1), 0, 1, null), 1);
assert.equal(cancelled(), true);
assert.equal(remainingMs(), 0);
display.send("outside");
