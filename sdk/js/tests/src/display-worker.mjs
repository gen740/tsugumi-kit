import assert from "node:assert/strict";
import { recv, send, viewOpen } from "@tsugumi/card";

const { status, bytes } = recv();
assert.equal(status, "message");
const { route, session } = viewOpen("background");
assert.equal(route[0], 0x55);
assert.equal(session[0], 0x44);
assert.equal(send(session, bytes), 0);
