import { writeFile } from "node:fs/promises";
import { join } from "node:path";
import { Artifact, Failure, FailureKind, Text, params } from "@tsugumi/card";

/** @type {import("@tsugumi/card").Card} */
export default async function run(_input, { cancelled, display }) {
  if (cancelled()) throw new Failure(FailureKind.cancelled, "Stopped before writing");
  const [file, directory] = Artifact.dir();
  const bytes = new TextEncoder().encode(params.text);
  await writeFile(join(directory, "note.txt"), bytes);
  display.send(new Text(`Wrote ${bytes.length} bytes to note.txt`));
  return { file };
}
