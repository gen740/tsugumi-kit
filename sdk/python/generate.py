"""Generate the Python Card API and its ABI payload verifier."""

import json
import keyword
import re
import shutil
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from card_cpp import IDENTIFIER, cpp_type, generate_headers, normalize_schema, schema_tokens, validators
from tsugumi_ports import load_card_schemas, param_members, reflect_schema, write_api_readme


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
    names.pop("tsugumi", None)
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


def params_fields(card):
    reserved = set(keyword.kwlist) | {"Params"}
    return list(zip(card.params, param_members((p.name for p in card.params), reserved)))


def param_type(type_):
    if type_.startswith("["):
        return f"tuple[{param_type(type_[1:-1])}, ...]"
    return {"bool": "bool", "long": "int", "double": "float", "string": "str"}[type_]


def embed_params(interface, values_file, output):
    values = json.loads(values_file.read_text())
    types = json.loads((interface / "param-types.json").read_text())
    lines = ["from card import Params", "", "params = Params("]
    for member, (name, type_) in zip(param_members(types, set(keyword.kwlist) | {"Params"}), types.items()):
        value = tuple(values[name]) if type_.startswith("[") else values[name]
        lines.append(f"    {member}={value!r},")
    output.write_text("\n".join([*lines, ")", ""]), encoding="utf-8")


def generate_python(card, names, output_schema, out):
    aliases = {}
    imports = []

    def python_type(type_):
        return ".".join(names.get(part, part) for part in type_.split("."))

    def import_type(module):
        if module not in aliases:
            native = module.rsplit(".", 1)[-1] + "T"
            alias = aliases[module] = f"_Type{len(aliases)}"
            imports.append(f"from {module} import {native} as {alias}")
        return aliases[module]

    def annotation(port, *, qualified=False):
        types = ([member.type for member in port.candidates] or [port.type])
        return " | ".join(
            python_type(type_) + "T" if qualified else import_type(python_type(type_))
            for type_ in types
        )

    catalogs = ("inputs.Inputs", output_schema["root_table"]["name"])

    inputs = list(port_members(card.inputs, {"params", "get", "require"}))
    outputs = list(port_members(card.outputs, set()))
    body = ["", "@dataclass(frozen=True)", "class Params:"]
    body.extend(f"    {member}: {param_type(param.type)}" for param, member in params_fields(card))
    if not card.params:
        body.append("    pass")
    stubs = [*body, "", "params: Params"]
    runtime_bindings = []
    for side, cls, members in (("inputs", "Input", inputs), ("outputs", "Output", outputs)):
        body.extend(["", "", "@dataclass", f"class {cls}:"])
        stubs.extend(["", "", "@dataclass", f"class {cls}:"])
        if not members:
            body.append("    pass")
            stubs.append(body[-1])
        entries = []
        for port, member in members:
            type_ = annotation(port)
            declaration = f"    {member}: {type_} | None"
            body.append(declaration + " = None")
            stubs.append(declaration + " = None")
            if side == "inputs":
                types = ", ".join(f"{v.schema!r}: {import_type(python_type(v.type))}" for v in port.candidates)
                entries.append(f"    ({port.name!r}, {member!r}, {{{types}}}),")
            else:
                table = import_type(python_type(port.type))
                entries.append(f"    ({port.name!r}, {member!r}, {table}, {port.required!r}),")
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

    stub_imports = list(imports)
    union_checks = generate_union_checks(output_schema, import_type, set(catalogs))
    header = [
        "# Generated by the Tsugumi Python SDK.",
        "from dataclasses import dataclass",
        "from tsugumi.Artifact import ArtifactT as Artifact",
    ]
    (out / "python/card.py").write_text("\n".join([
        *header, "from tsugumi import Failure", *imports, *body, *runtime_bindings,
        "_input_members = {name: member for name, member, *_ in _inputs}", "",
        *union_checks,
        "def __getattr__(name):",
        '    if name == "params":',
        '        from card_params import params',
        '        return params',
        '    raise AttributeError(name)',
        "",
    ]), encoding="utf-8")
    (out / "python/card.pyi").write_text("\n".join([
        *header, "from typing import Literal, overload", *stub_imports, *stubs, "",
    ]), encoding="utf-8")
    for module in catalogs:
        (out / "python").joinpath(*module.split(".")).with_suffix(".py").unlink(missing_ok=True)
    write_api_readme(
        out / "README.md",
        ((port.name, annotation(port, qualified=True) + " | None") for port, _ in inputs),
        ((port.name, annotation(port, qualified=True) + " | None"
          + (" (required output)" if port.required else "")) for port, _ in outputs),
        ((param.name, param.type) for param in card.params),
    )


def generate_cpp(card, python_names, cpp_names, output_schema, out):
    def table(type_):
        return cpp_type(".".join(python_names.get(part, part) for part in type_.split(".")), cpp_names)

    lines = [
        "// Generated by the Tsugumi Python SDK.",
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "python_card.hh"',
        "",
        "namespace {",
        "using namespace tsugumi::abi;",
        "",
        *validators(output_schema, [table(port.type) for port in card.outputs]),
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  PythonInput input(bytes);",
    ]
    for port in card.inputs:
        name = json.dumps(port.name)
        lines += [f"  if (const auto entry = input.Take({name})) {{"]
        for index, candidate in enumerate(port.candidates):
            condition = "if" if index == 0 else "else if"
            match = f'entry->schema == "{candidate.schema}"' if candidate.schema else "true"
            lines += [f"    {condition} ({match})",
                      f"      input.Read<{table(candidate.type)}>({name}, *entry, \"{candidate.schema}\");"]
        lines += ['    else Reject(FailureKind::input_invalid, "Unknown schema for " + std::string('
                  f"{name}));", "  }"]
    lines += ["  input.CheckConsumed();", "  auto result = RunPython(input);"]
    if card.outputs:
        lines.append("  for (auto& port : result.outputs) {")
        for port in card.outputs:
            lines += [
                f"    if (port->name == {json.dumps(port.name)}) {{",
                f"      VerifyOutput<{table(port.type)}>(*port, VisitArtifacts);",
                "      continue;",
                "    }",
            ]
        lines += ['    Reject(FailureKind::output_invalid, "Unknown output " + port->name);', "  }"]
    lines.extend([
        "  return result;",
        "}",
        "}  // namespace",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi::Invoke(call, result, RunCard);",
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
        abi_python = work / "abi-python"
        for schema in sorted(work.rglob("*.fbs")):
            sdk_type = schema.parent != work / "tsugumi" or schema.name == "artifact.fbs"
            destination = out / "python" if sdk_type else abi_python
            command = ["flatc", "--python", "--gen-object-api",
                       "-I", str(work), "-o", str(destination), str(schema.relative_to(work))]
            subprocess.run([*command[:-1], "--python-typing", command[-1]], cwd=work, check=True)
            # Keep flatc's stubs and regenerate the runtime with deferred
            # imports so mutually recursive table types can load.
            subprocess.run(command, cwd=work, check=True)
        wire = out / "wire/Tsugumi"
        wire.mkdir(parents=True)
        for package in ("Card", "Data"):
            shutil.copytree(abi_python / "Tsugumi" / package, wire / package)
        shutil.copytree(abi_python / "tsugumi/builtin", out / "python/tsugumi/builtin", dirs_exist_ok=True)
        artifact = out / "python/tsugumi/Artifact.py"
        marker = "class ArtifactT(object):"
        artifact.write_text(artifact.read_text().replace(marker, marker + """
    @classmethod
    def dir(cls):
        from tsugumi._artifact import create

        return create(cls)

    @classmethod
    def upload(cls, source):
        from tsugumi._artifact import upload

        return upload(cls, source)

    def open(self):
        from tsugumi._artifact import open_artifact

        return open_artifact(self)
"""), encoding="utf-8")
        stub = artifact.with_suffix(".pyi")
        stub.write_text(stub.read_text().replace(marker, marker + """
  @classmethod
  def dir(cls) -> tuple[ArtifactT, pathlib.Path]: ...
  @classmethod
  def upload(cls, source: str | os.PathLike[str]) -> ArtifactT: ...
  def open(self) -> pathlib.Path: ...
""").replace("import typing", "import os\nimport pathlib\nimport typing"), encoding="utf-8")
        for path in (out / "python").rglob("*.py*"):
            source = path.read_text(encoding="utf-8")
            fixed = fix_nested_accessors(source)
            if fixed != source:
                path.write_text(fixed, encoding="utf-8")
        outputs = reflect_schema(work / "_outputs.fbs", reflection)
        cpp_names, cpp_output = generate_headers(work, out, reflection)
        generate_python(card, names, outputs, out)
        generate_cpp(card, names, cpp_names, cpp_output, out / "card_adapter.cc")


if __name__ == "__main__":
    if sys.argv[1] == "--params":
        embed_params(*(Path(path) for path in sys.argv[2:]))
    else:
        main(*(Path(path).resolve() for path in sys.argv[1:]))
