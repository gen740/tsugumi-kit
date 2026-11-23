"""Prepare the three catalogs used by the language generators."""

import re
import shutil
import sys
from pathlib import Path


TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
    r'[A-Za-z_][A-Za-z_0-9]*|\d[\w.]*|[^\s]'
)


def schema_tokens(source):
    return [token for token in TOKEN.finditer(source)
            if not token.group().startswith(("//", "/*"))]


def copy_outputs(source, directory):
    """Copy the authored root and its relative include closure unchanged."""
    root = source.parent
    pending = [(source, directory / "_outputs.fbs")]
    copied = set()
    while pending:
        file, target = pending.pop()
        if target in copied:
            continue
        copied.add(target)
        tokens = schema_tokens(file.read_text(encoding="utf-8"))
        for index, token in enumerate(tokens[:-1]):
            if token.group() == "include" and tokens[index + 1].group().startswith('"'):
                include = tokens[index + 1].group()[1:-1]
                child = (file.parent / include).resolve()
                relative = child.relative_to(root)
                destination = directory / relative
                if relative.name in {"_inputs.fbs", "_params.fbs"} or destination == directory / "_outputs.fbs":
                    raise ValueError(f"{file}: include uses a reserved catalog path: {include}")
                pending.append((child, destination))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)


def main(out, inputs, outputs, params):
    out = Path(out)
    out.mkdir()
    shutil.copyfile(inputs, out / "_inputs.fbs")
    copy_outputs(Path(outputs), out)
    shutil.copyfile(params, out / "_params.fbs")


if __name__ == "__main__":
    main(*sys.argv[1:])
