import { parentPort } from "node:worker_threads";
import { display } from "@tsugumi/card";

// Keep running JavaScript until the host process exits.
parentPort.postMessage("running");
for (;;) display.send("exit");
