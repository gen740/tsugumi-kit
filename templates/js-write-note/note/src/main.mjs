import { writeFile } from "node:fs/promises";
import { Failure, FailureKind, Text } from "@tsugumi/card";

/** @type {import("@tsugumi/card").Card} */
export default async function run({ params }, { cancelled, display }) {
  if (cancelled()) throw new Failure(FailureKind.cancelled, "Stopped before writing");
  const file = "note.txt";
  const bytes = new TextEncoder().encode(params.text ?? "");
  await writeFile(file, bytes);
  display.send(new Text(`Wrote ${bytes.length} bytes to ${file}`));
  return { file };
}
