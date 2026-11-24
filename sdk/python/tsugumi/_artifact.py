"""Bind the FlatBuffers Artifact native object to Card effects."""

import shutil
from pathlib import Path

import _tsugumi_host
from tsugumi.ArtifactId import ArtifactIdT


def create(cls, keep=True):
    identifier, path = _tsugumi_host.artifact_create(bool(keep))
    return cls(id=ArtifactIdT(bytes=list(identifier))), Path(path)


def upload(cls, source, keep=True):
    source = Path(source)
    artifact, target = create(cls, keep)
    if source.is_dir():
        shutil.copytree(source, target, dirs_exist_ok=True)
    else:
        shutil.copy2(source, target / source.name)
    return artifact


def open_artifact(artifact):
    return Path(_tsugumi_host.artifact_open(bytes(artifact.id.bytes)))
