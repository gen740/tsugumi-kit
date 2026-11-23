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


def copy_catalog(source, directory, name):
    """Copy the authored root and its relative include closure unchanged."""
    root = source.parent
    pending = [(source, directory / name)]
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
                if include in {"tsugumi/artifact.fbs", "tsugumi/card.fbs"}:
                    continue
                if relative.name in {"_inputs.fbs", "_outputs.fbs"}:
                    raise ValueError(f"{file}: include uses a reserved catalog path: {include}")
                pending.append((child, destination))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)


def main(out, inputs, outputs, params, abi):
    out = Path(out)
    out.mkdir()
    shutil.copytree(abi, out / "tsugumi", ignore=shutil.ignore_patterns("*.h"))
    if inputs == "-":
        (out / "_inputs.fbs").write_text("namespace inputs; table Inputs {} root_type Inputs;\n")
    else:
        copy_catalog(Path(inputs), out, "_inputs.fbs")
    # Share the fixed Collect definitions with the ABI adapter even when
    # the generated input catalog is authored with inline declarations.
    input_catalog = out / "_inputs.fbs"
    card_schema = (out / "tsugumi/card.fbs").read_text()
    declarations = "namespace tsugumi.builtin;" + card_schema.split(
        "namespace tsugumi.builtin;", 1
    )[1]
    pattern = r"\s*".join(re.escape(part) for part in re.findall(r"\w+|[^\s\w]", declarations))
    text, replaced = re.subn(pattern, "", input_catalog.read_text(), count=1)
    if replaced:
        input_catalog.write_text('include "tsugumi/card.fbs";\n' + text)
    copy_catalog(Path(outputs), out, "_outputs.fbs")
    shutil.copyfile(params, out / "param-types.json")


if __name__ == "__main__":
    main(*sys.argv[1:])
