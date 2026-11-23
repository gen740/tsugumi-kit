import { recv, send } from "@tsugumi/card";

const { status, bytes } = recv("background");
if (status === "message") send("background", bytes);
