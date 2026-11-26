"""Drop store references to the Python package outputs copied into a Card."""

import re
import subprocess
import sys
from collections import defaultdict
from itertools import batched
from pathlib import Path


def loader_paths(path):
    kind = subprocess.check_output(["file", "-b", path])
    if kind.startswith(b"ELF "):
        commands = subprocess.check_output(["readelf", "-dW", path])
        return re.findall(
            rb"\((?:NEEDED|RPATH|RUNPATH|SONAME|FILTER|AUXILIARY|AUDIT|DEPAUDIT)\).*?\[(.*)\]",
            commands,
        )
    if kind.startswith(b"Mach-O "):
        commands = subprocess.check_output(["otool", "-l", path])
        return re.findall(rb"^\s*(?:name|path) (.+) \(offset \d+\)$", commands, re.M)
    return []


def strip_package_references(root, site_packages, outputs):
    packages = {
        path.name.split("-", 1)[0].encode(): str(path)
        for path in map(Path, outputs)
        if (path / site_packages).is_dir()
    }
    if not packages:
        return
    hashes = re.compile(b"|".join(packages))
    matches = defaultdict(list)

    # Cython embeds .pxd paths for tracebacks. Refuse to invalidate any path
    # used by the native loader, including paths written relative to the binary.
    for path in root.rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        references = set(hashes.findall(path.read_bytes()))
        if not references:
            continue
        for loader_path in loader_paths(path):
            match = hashes.search(loader_path)
            if match:
                sys.exit(
                    f"pythonCard: {path.relative_to(root)} has a native loader path "
                    f"{loader_path.decode(errors='replace')!r} referencing the copied "
                    f"Python package {packages[match[0]]}. Relocate the library or "
                    "search path before removing package references."
                )
        matches[tuple(sorted(references))].append(path)

    for references, paths in matches.items():
        targets = [arg for ref in references for arg in ("-t", packages[ref])]
        for batch in batched(paths, 128):
            subprocess.run(["remove-references-to", *targets, *batch], check=True)


if __name__ == "__main__":
    root, site_packages, *outputs = sys.argv[1:]
    strip_package_references(Path(root), site_packages, outputs)
