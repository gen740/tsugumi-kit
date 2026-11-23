import { ArtifactT } from "./tsugumi/artifact.js";
import { ArtifactIdT } from "./tsugumi/artifact-id.js";
import { artifactEffect } from "./effects.js";

// Extend the generated object itself so unpacking also yields an Artifact.
export const Artifact = ArtifactT;

Artifact.dir = function () {
  const [id, directory] = artifactEffect("dir");
  return [new Artifact(new ArtifactIdT(Array.from(id))), directory];
};

Artifact.upload = function (path) {
  const [id] = artifactEffect("upload", path);
  return new Artifact(new ArtifactIdT(Array.from(id)));
};

Artifact.prototype.open = function () {
  const [, directory] = artifactEffect("open", Uint8Array.from(this.id.bytes));
  return directory;
};
