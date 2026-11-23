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


def input_port(name, file):
    text = Path(file).read_text(encoding="utf-8")
    tokens = schema_tokens(text)
    words = [token.group() for token in tokens]
    prefix = ["attribute", '"tsugumi"', ";"]
    suffix = ["}", "root_type", "Inputs", ";"]
    wrapper = ["namespace", "inputs", ";", "table", "Inputs", "{"]
    start = next((i for i in range(len(words) - len(wrapper), -1, -1)
                  if words[i:i + len(wrapper)] == wrapper), None)
    if words[:len(prefix)] != prefix or words[-len(suffix):] != suffix or start is None:
        raise ValueError(f"{file}: expected an inputs.Inputs catalog for {name}")
    field = tokens[start + len(wrapper):-len(suffix)]
    field_words = [token.group() for token in field]
    if (field_words[:2] != [name, ":"] or field_words[-1:] != [";"]
            or field_words.count(";") != 1):
        raise ValueError(f"{file}: expected a single field named {name}")
    index = next((i + 2 for i in range(len(field_words) - 2)
                  if field_words[i:i + 3] == ["id", ":", "0"]), None)
    if index is None:
        raise ValueError(f"{file}: expected field ID 0 for {name}")
    id_token = field[index]
    closure = text[tokens[len(prefix) - 1].end():tokens[start].start()].strip()
    before = text[field[0].start():id_token.start()]
    after = text[id_token.end():field[-1].end()]
    return closure, before, after


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


def main(out, outputs, params, *ports):
    out = Path(out)
    out.mkdir()
    closures, fields = [], []
    for index, (name, file) in enumerate(zip(ports[::2], ports[1::2], strict=True)):
        closure, before, after = input_port(name, file)
        if closure:
            closures.append(closure)
        fields.append(f"  {before}{index}{after}")
    (out / "_inputs.fbs").write_text("\n".join([
        'attribute "tsugumi";', *closures, "namespace inputs;", "table Inputs {",
        *fields, "}", "root_type Inputs;", "",
    ]), encoding="utf-8")
    copy_outputs(Path(outputs), out)
    shutil.copyfile(params, out / "_params.fbs")


if __name__ == "__main__":
    main(*sys.argv[1:])
