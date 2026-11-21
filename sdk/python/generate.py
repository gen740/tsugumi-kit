"""Generate the Python Card types and the typed wire checks for its adapter."""

import json
import keyword
import re
import sys
from pathlib import Path

KINDS = {"flatbuffer": "FlatBuffer", "file": "File", "directory": "Directory"}
SCHEMA_TOKEN = re.compile(
    r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/|[a-zA-Z_]\w*(?:\.[a-zA-Z_]\w*)*'
)


def prepare_contract(cpp, source):
    sys.dont_write_bytecode = True
    sys.path.insert(0, cpp)
    from card_cpp import KEYWORDS

    reserved = KEYWORDS | set(keyword.kwlist)
    contract = json.loads((source / "_contract.json").read_text())
    for side in ("inputs", "outputs"):
        schema = (source / f"_{side}.fbs").read_text()
        used = {port["name"] for port in contract[side]} | reserved
        namespaces = {}
        for port in contract[side]:
            name = port["name"]
            if name not in reserved:
                continue
            escaped = name + "_"
            while escaped in used:
                escaped += "_"
            used.add(escaped)
            namespaces[f"{side}.{name}"] = f"{side}.{escaped}"

        def rename(qualified):
            parts = qualified.split(".", 2)
            prefix = ".".join(parts[:2])
            return ".".join([namespaces.get(prefix, prefix), *parts[2:]])

        if namespaces:
            # Core-generated schemas use fully qualified names for port types.
            # Keep field names, comments and string literals intact.
            schema = SCHEMA_TOKEN.sub(
                lambda match: rename(match[0]) if match[0].startswith(f"{side}.")
                else match[0],
                schema,
            )
            for port in contract[side]:
                port["type"] = rename(port["type"])
        Path(f"_{side}.fbs").write_text(schema)
    Path("_params.fbs").write_text((source / "_params.fbs").read_text())
    Path("_contract.json").write_text(json.dumps(contract))


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


def generate_union_checks(schema, import_type):
    objects = schema["objects"]
    unions = {}
    children = {}
    for index, obj in enumerate(objects):
        if obj["is_struct"]:
            continue
        fields = obj.get("fields", [])
        fields_by_id = {field["id"]: field["name"] for field in fields}
        for field in fields:
            if field["deprecated"]:
                continue
            spec = field["type"]
            member = field_member(field["name"])
            target = spec["index"]
            if spec["base_type"] == "Union":
                tag_member = field_member(fields_by_id[field["id"] - 1])
                variants = {
                    v["value"]: v["union_type"]["index"]
                    for v in schema["enums"][target]["values"] if v["value"]
                }
                unions.setdefault(index, []).append((member, tag_member, variants))
            else:
                vector = spec["base_type"] in ("Vector", "Vector64")
                if (spec["element"] if vector else spec["base_type"]) != "Obj":
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
            types = ", ".join(f"{tag}: {alias(target)}" for tag, target in variants.items())
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
        member = port["name"]
        if not member.isidentifier():
            raise ValueError(f"Invalid Python port name: {member!r}")
        if member.startswith("__"):
            member = "port" + member
        while keyword.iskeyword(member) or member in used:
            member += "_"
        used.add(member)
        yield member, port


def generate_python(contract, schema, out):
    imports = ["from dataclasses import dataclass", "from parameters.Params import ParamsT"]
    aliases = {}

    def import_type(module):
        if module not in aliases:
            native = module.rsplit(".", 1)[-1] + "T"
            alias = aliases[module] = f"_Type{len(aliases)}"
            imports.append(f"from {module} import {native} as {alias}")
        return aliases[module]

    body = []
    for side, cls in (("inputs", "Input"), ("outputs", "Output")):
        members = list(port_members(contract[side], {"params"} if side == "inputs" else set()))
        body.extend(["", "@dataclass", f"class {cls}:"])
        if side == "inputs":
            body.append("    params: ParamsT")
        elif not members:
            body.append("    pass")

        bindings = []
        for member, port in members:
            alias = import_type(port["type"])
            optional = side == "outputs" or not port["required"]
            annotation = f"{alias} | None" if optional else alias
            default = " = None" if side == "outputs" else ""
            body.append(f"    {member}: {annotation}{default}")
            bindings.append(f"    ({port['name']!r}, {member!r}, {alias}, "
                            f"PortKind.{KINDS[port['kind']]}, {port['required']!r}),")
        body.extend(["", f"_{side} = (", *bindings, ")", ""])

    body.extend(generate_union_checks(schema, import_type))
    out.write_text("\n".join([
        "# Generated by the Tsugumi ABI 9 Python SDK.",
        "from Tsugumi.Data.PortKind import PortKind",
        *imports, *body,
    ]))


def generate_cpp(contract, out):
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
    for port in contract["inputs"]:
        table = "::" + port["type"].replace(".", "::")
        name = json.dumps(port["name"], ensure_ascii=False)
        kind = KINDS[port["kind"]]
        required = str(port["required"]).lower()
        lines.append(f"  input.VerifyPort<{table}>({name}, PortKind::{kind}, {required});")
    lines += [
        "  input.CheckConsumed();",
        '  VerifyValue<::parameters::Params>(input.Params(), "params");',
        "  auto result = RunPython(input.Envelope());",
        "  for (const auto& port : result.outputs) {",
    ]
    for port in contract["outputs"]:
        table = "::" + port["type"].replace(".", "::")
        name = json.dumps(port["name"], ensure_ascii=False)
        lines += [
            f"    if (port->name == {name}) {{",
            f"      VerifyOutput<{table}>(*port);",
            "      continue;",
            "    }",
        ]
    lines += [
        '    Reject(FailureKind::output_invalid, "Unknown output " + port->name);',
        "  }",
        "  return result;",
        "}",
        "}  // namespace",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi9::Invoke(call, result, RunCard);",
        "}",
        "",
    ]
    out.write_text("\n".join(lines))


if __name__ == "__main__":
    if sys.argv[1] == "prepare":
        prepare_contract(sys.argv[2], Path(sys.argv[3]))
    else:
        contract = json.loads(Path("_contract.json").read_text())
        schema = json.loads(Path("_outputs.json").read_text())
        generate_python(contract, schema, Path(sys.argv[2]))
        generate_cpp(contract, Path(sys.argv[3]))
