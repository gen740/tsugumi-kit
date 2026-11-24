import { ArtifactT } from "./tsugumi/artifact.js";
import { ArtifactIdT } from "./tsugumi/artifact-id.js";
import { artifactEffect } from "./effects.js";

// Extend the generated object itself so unpacking also yields an Artifact.
export const Artifact = ArtifactT;

// keep: false makes an Artifact that exists only while its run runs.
Artifact.dir = function ({ keep = true } = {}) {
  const [id, directory] = artifactEffect("dir", undefined, keep);
  return [new Artifact(new ArtifactIdT(Array.from(id))), directory];
};

Artifact.upload = function (path, { keep = true } = {}) {
  const [id] = artifactEffect("upload", path, keep);
  return new Artifact(new ArtifactIdT(Array.from(id)));
};

Artifact.prototype.open = function () {
  const [, directory] = artifactEffect("open", Uint8Array.from(this.id.bytes));
  return directory;
};
