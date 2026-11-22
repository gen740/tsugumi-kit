"""Generate the Python Card API and its ABI payload verifier."""

import json
import keyword
import re
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from card_cpp import IDENTIFIER, cpp_type, generate_headers, normalize_schema, schema_tokens
from tsugumi_ports import load_card_schemas, reflect_schema, write_api_readme


def fix_nested_accessors(source):
    # flatc emits "from package.Type import package.Type" for qualified
    # nested_flatbuffer attributes. Alias the referenced class in its place.
    path = r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)+"
    imports = re.compile(rf"(?m)^( *)from ({path}) import \2$")
    aliases = {}

    def fix_import(match):
        indent, module = match.groups()
        alias = aliases.setdefault(module, f"_NestedFlatBuffer{len(aliases)}")
        return f"{indent}from {module} import {module.rsplit('.', 1)[-1]} as {alias}"

    source = imports.sub(fix_import, source)
    for module, alias in aliases.items():
        source = source.replace(f"Union[{module}, int]", f"Union[{alias}, int]")
        source = source.replace(f"return {module}.GetRootAs", f"return {alias}.GetRootAs")
    return source


def python_schemas(schemas, destination):
    # flatc escapes Python keywords in imports, but leaves them in generated
    # directory names. Normalize identifiers before all code generation so
    # references and paths agree, including inside nested_flatbuffer attributes.
    sources = {path.relative_to(schemas): path.read_text(encoding="utf-8")
               for path in schemas.rglob("*.fbs") if not path.name.startswith("._")}
    tokens = {path: schema_tokens(source) for path, source in sources.items()}
    occupied = {token.group() for matches in tokens.values() for token in matches
                if IDENTIFIER.fullmatch(token.group())}
    names = {}
    for name in sorted(occupied.intersection(keyword.kwlist)):
        candidate = name + "_"
        while candidate in occupied:
            candidate += "_"
        names[name] = candidate
        occupied.add(candidate)
    for path, source in sources.items():
        target = destination / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(normalize_schema(source, tokens[path], names), encoding="utf-8")
    return names


def field_member(name):
    # Match flatc's CamelToSnake and ToCamelCase for Python object fields.
    snake = [name[0].lower()]
    for previous, char in zip(name, name[1:]):
        if char != "_" and not char.islower() and (
            previous.islower() or (previous.isdigit() and not char.isdigit())
        ):
            snake.append("_")
        snake.append(char.lower())
    name = "".join(snake)
    if name.startswith("_") and name[1:2].isalpha():
        name = "_" + name[1].upper() + name[2:]
    name = name[0] + re.sub(r"_(.)", lambda m: m[1].upper(), name[1:])
    return name + "_" if keyword.iskeyword(name) else name


def generate_union_checks(schema, import_type, hidden):
    # These checks are for ordinary unions in Card-authored output objects.
    # The SDK builds Merge's own boxes and tags from the shared port metadata.
    objects = schema["objects"]
    unions = {}
    children = {}
    for index, obj in enumerate(objects):
        if obj.get("is_struct", False) or obj["name"] in hidden:
            continue
        fields = obj.get("fields", [])
        fields_by_id = {field.get("id", 0): field["name"] for field in fields}
        for field in fields:
            if field.get("deprecated", False):
                continue
            spec = field["type"]
            member = field_member(field["name"])
            target = spec.get("index", -1)
            if spec.get("base_type") == "Union":
                tag_member = field_member(fields_by_id[field["id"] - 1])
                variants = {
                    v["value"]: v["union_type"].get("index", -1)
                    for v in schema["enums"][target]["values"] if v.get("value", 0)
                }
                unions.setdefault(index, []).append((member, tag_member, variants))
            else:
                vector = spec.get("base_type") in ("Vector", "Vector64")
                if (spec.get("element") if vector else spec.get("base_type")) != "Obj":
                    continue
                children.setdefault(index, []).append(
                    (member, target, vector)
                )

    # Only visit child tables that can contain unions, even in recursive schemas.
    checked = set(unions)
    while True:
        parents = {i for i, fields in children.items()
                   if any(target in checked for _, target, _ in fields)}
        if parents <= checked:
            break
        checked.update(parents)

    def alias(index):
        return import_type(objects[index]["name"])

    lines = ["_union_checks = {"]
    for index in sorted(checked):
        lines.append(f"    {alias(index)}: ((")
        for member, tag_member, variants in unions.get(index, []):
            types = ", ".join(f"{tag}: {alias(target) if target >= 0 else '(str, bytes)'}"
                              for tag, target in variants.items())
            lines.append(f"        ({member!r}, {tag_member!r}, {{{types}}}),")
        lines.append("    ), (")
        for member, target, vector in children.get(index, []):
            if target in checked:
                lines.append(f"        ({member!r}, {alias(target)}, {vector!r}),")
        lines.append("    )),")
    return [*lines, "}", ""]


def port_members(ports, reserved):
    used = set(reserved)
    for port in ports:
        member = port.name
        if member.startswith("__"):
            member = "port" + member
        while keyword.iskeyword(member) or member in used:
            member += "_"
        used.add(member)
        yield port, member


def merge_leaves(type_, merge):
    if not merge:
        yield type_
    for member in merge:
        yield from merge_leaves(member.type, member.merge)


def generate_python(card, names, output_schema, out):
    aliases = {}
    imports = []

    def python_type(type_):
        return ".".join(names.get(part, part) for part in type_.split("."))

    def import_type(module):
        if module == "parameters.Params":
            return "ParamsT"
        if module not in aliases:
            native = module.rsplit(".", 1)[-1] + "T"
            alias = aliases[module] = f"_Type{len(aliases)}"
            imports.append(f"from {module} import {native} as {alias}")
        return aliases[module]

    def annotation(port, *, output=False, qualified=False):
        if port.kind == "Path":
            return "str | PathLike[str]" if output else "str"
        types = (python_type(type_) for type_ in merge_leaves(port.type, port.merge))
        return " | ".join(type_ + "T" if qualified else import_type(type_) for type_ in types)

    catalogs = ("inputs.Inputs", "outputs.Outputs")
    hidden = set(catalogs)

    def bindings(type_, merge):
        cls = import_type(python_type(type_))
        if not merge:
            return cls, "{}"
        hidden.add(python_type(type_))
        hidden.add(python_type(type_.rsplit(".", 1)[0] + ".Arrival"))
        entries = []
        for member in merge:
            box_name = python_type(member.box_type)
            hidden.add(box_name)
            inner, cases = bindings(member.type, member.merge)
            entries.append(f"{member.value}: ({import_type(box_name)}, {inner}, {cases})")
        return cls, "{" + ", ".join(entries) + "}"

    inputs = list(port_members(card.inputs, {"params", "get", "require"}))
    outputs = list(port_members(card.outputs, set()))
    body = []
    stubs = []
    runtime_bindings = []
    for side, cls, members in (("inputs", "Input", inputs), ("outputs", "Output", outputs)):
        body.extend(["", "", "@dataclass", f"class {cls}:"])
        stubs.extend(["", "", "@dataclass", f"class {cls}:"])
        if side == "inputs":
            body.append("    params: ParamsT")
            stubs.append(body[-1])
        elif not members:
            body.append("    pass")
            stubs.append(body[-1])
        entries = []
        for port, member in members:
            type_ = annotation(port, output=side == "outputs")
            declaration = f"    {member}: {type_} | None"
            body.append(declaration + " = None")
            stubs.append(declaration + " = None")
            table, cases = bindings(port.type, port.merge) if port.kind == "FlatBuffer" else ("None", "{}")
            entries.append(f"    ({port.name!r}, {member!r}, {table}, {cases}, {port.required!r}),")
        runtime_bindings.extend(["", f"_{side} = (", *entries, ")"])

        if side == "inputs":
            body.extend([
                "",
                "    def get(self, name: str) -> object | None:",
                "        member = _input_members.get(name)",
                "        return getattr(self, member) if member is not None else None",
                "",
                "    def require(self, name: str) -> object:",
                "        value = self.get(name)",
                "        if value is None:",
                '            raise Failure("input_missing", f"Missing input {name}")',
                "        return value",
            ])
            for method in ("get", "require"):
                stubs.append("")
                for port, _ in members:
                    type_ = annotation(port)
                    if method == "get":
                        type_ += " | None"
                    stubs.extend(["    @overload",
                                  f"    def {method}(self, name: Literal[{port.name!r}]) -> {type_}: ..."])
                if members:
                    stubs.append("    @overload")
                fallback = "object | None" if method == "get" else "object"
                stubs.append(f"    def {method}(self, name: str) -> {fallback}: ...")

    private_aliases = {aliases[module] for module in hidden if module in aliases}
    public_imports = [line for line in imports if line.rsplit(" ", 1)[-1] not in private_aliases]
    union_checks = generate_union_checks(output_schema, import_type, hidden)
    header = [
        "# Generated by the Tsugumi ABI 9 Python SDK.",
        "from dataclasses import dataclass",
        "from os import PathLike",
        "from parameters.Params import ParamsT as ParamsT",
    ]
    (out / "python/card.py").write_text("\n".join([
        *header, "from tsugumi import Failure", *imports, *body, *runtime_bindings,
        "_input_members = {name: member for name, member, *_ in _inputs}", "",
        *union_checks,
    ]), encoding="utf-8")
    (out / "python/card.pyi").write_text("\n".join([
        *header, "from typing import Literal, overload", *public_imports, *stubs, "",
    ]), encoding="utf-8")
    for module in hidden:
        (out / "python").joinpath(*module.split(".")).with_suffix(".pyi").unlink(missing_ok=True)
    for module in catalogs:
        (out / "python").joinpath(*module.split(".")).with_suffix(".py").unlink(missing_ok=True)
    write_api_readme(
        out / "README.md",
        ((port.name, annotation(port, qualified=True) + " | None") for port, _ in inputs),
        ((port.name, annotation(port, output=True, qualified=True) + " | None"
          + (" (required output)" if port.required else "")) for port, _ in outputs),
        ((param.name, param.type) for param in card.params),
    )


def generate_cpp(card, python_names, cpp_names, out):
    def table(type_):
        return cpp_type(".".join(python_names.get(part, part) for part in type_.split(".")), cpp_names)

    lines = [
        "// Generated by the Tsugumi ABI 9 Python SDK.",
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        '#include "python_card.hh"',
        "",
        "namespace {",
        "using namespace tsugumi::abi9;",
        "",
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  PythonInput input(bytes);",
    ]
    for port in card.inputs:
        name = json.dumps(port.name)
        if port.kind == "Path":
            lines.append(f"  input.ReadPath({name});")
        else:
            lines.append(f"  input.ReadPort<{table(port.type)}>({name});")
    lines.extend([
        "  input.CheckConsumed();",
        f'  VerifyValue<{table(card.params_type)}>(input.Params(), "params");',
        "  auto result = RunPython(input);",
    ])
    flat_outputs = [port for port in card.outputs if port.kind == "FlatBuffer"]
    if flat_outputs:
        lines.extend([
            "  for (const auto& port : result.outputs) {",
            "    if (port->kind == PortKind::Path) continue;",
        ])
        for port in flat_outputs:
            lines.extend([
                f"    if (port->name == {json.dumps(port.name)}) {{",
                f"      VerifyOutput<{table(port.type)}>(*port);",
                "      continue;",
                "    }",
            ])
        lines.extend(['    Reject(FailureKind::output_invalid, "Unknown output " + port->name);', "  }"])
    lines.extend([
        "  return result;",
        "}",
        "}  // namespace",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi9::Invoke(call, result, RunCard);",
        "}",
        "",
    ])
    out.write_text("\n".join(lines), encoding="utf-8")


def main(schemas, out, reflection):
    card = load_card_schemas(schemas, reflection)
    (out / "python").mkdir(parents=True)
    (out / "include").mkdir()
    with TemporaryDirectory() as directory:
        work = Path(directory)
        names = python_schemas(schemas, work)
        for side in ("inputs", "outputs", "params"):
            command = ["flatc", "--python", "--gen-object-api",
                       "-o", str(out / "python"), f"_{side}.fbs"]
            subprocess.run([*command[:-1], "--python-typing", command[-1]], cwd=work, check=True)
            # Keep flatc's stubs and regenerate the runtime with deferred
            # imports so mutually recursive table types can load.
            subprocess.run(command, cwd=work, check=True)
        for path in (out / "python").rglob("*.py*"):
            source = path.read_text(encoding="utf-8")
            fixed = fix_nested_accessors(source)
            if fixed != source:
                path.write_text(fixed, encoding="utf-8")
        outputs = reflect_schema(work / "_outputs.fbs", reflection)
        cpp_names, _ = generate_headers(work, out, reflection)
        generate_python(card, names, outputs, out)
        generate_cpp(card, names, cpp_names, out / "card_adapter.cc")


if __name__ == "__main__":
    main(*(Path(path).resolve() for path in sys.argv[1:]))
