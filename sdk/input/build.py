"""Encode each Input Card output as a FlatBuffer with an artifact sidecar."""

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from tsugumi_ports import load_ports, reflect_schema


def replace_artifacts(value, port, root, ids):
    if isinstance(value, list):
        return [replace_artifacts(item, port, root, ids) for item in value]
    if not isinstance(value, dict):
        return value
    if set(value) != {"__tsugumiArtifact"}:
        return {k: replace_artifacts(v, port, root, ids) for k, v in value.items()}
    path = value["__tsugumiArtifact"]
    source = Path(path)
    identifier = hashlib.sha256(port.encode() + b"\0" + path.encode()).digest()[:16]
    if identifier not in ids:
        target = root / identifier.hex()
        if source.is_dir():
            shutil.copytree(source, target)
        else:
            target.mkdir()
            # Nix prefixes the original basename with a hash at the store root.
            name = source.name.split("-", 1)[1] if source.parent == Path("/nix/store") else source.name
            shutil.copy2(source, target / name)
        ids.add(identifier)
    return {"id": {"bytes": list(identifier)}}


def main(schema, values_file, reflection, out):
    schema, out = Path(schema), Path(out)
    values = json.loads(Path(values_file).read_text())
    ports = {p.name: p for p in load_ports(reflect_schema(schema, reflection), schema)}
    if not isinstance(values, dict):
        raise ValueError("Expected an attribute set of output port values")
    if unknown := values.keys() - ports.keys():
        raise ValueError(f"Unknown output ports: {', '.join(sorted(unknown))}")
    # Core checks required ports when it emits this build's output, so it
    # can report output_invalid for a missing port.
    (out / "ports").mkdir(parents=True)
    (out / "artifacts").mkdir()
    with TemporaryDirectory() as directory:
        work = Path(directory)
        for name, value in values.items():
            ids = set()
            encoded = replace_artifacts(value, name, out / "artifacts", ids)
            data = work / "port.json"
            data.write_text(json.dumps(encoded))
            subprocess.run(
                ["flatc", "-b", "--strict-json", "-I", str(schema.parent),
                 "--root-type", ports[name].type, "-o", directory, str(schema), str(data)],
                check=True,
            )
            # A file_extension directive can change the generated filename.
            binary, = (path for path in work.iterdir() if path != data)
            shutil.move(binary, out / "ports" / name)
            (out / "ports" / f"{name}.artifacts").write_text(
                "".join(f"{identifier.hex()}\n" for identifier in sorted(ids))
            )


if __name__ == "__main__":
    main(*sys.argv[1:])
