"""Generate the JS contract bindings and native wire verifier at build time."""

import json
import re
import subprocess
import sys
from pathlib import Path


# Match the TypeScript object API names emitted by flatc.
TS_KEYWORDS = set("""
arguments break case catch class const continue debugger default delete do else
enum export extends false finally for function if import in instanceof new null
Object return super switch this throw true try typeof var void while with as
implements interface let package private protected public static yield
""".split())


def escape(name):
    return name + "_" if name in TS_KEYWORDS else name


def camel(name):
    snake = name[0].lower()
    for previous, char in zip(name, name[1:]):
        if char != "_" and not char.islower() and (
            previous.islower() or previous.isdigit() and not char.isdigit()
        ):
            snake += "_"
        snake += char.lower()
    if snake.startswith("_") and snake[1:2].isalpha():
        snake = "_" + snake[1].upper() + snake[2:]
    return snake[0] + re.sub(r"_(.)", lambda m: m[1].upper(), snake[1:])


def js_type(name):
    namespace, cls = name.rsplit(".", 1)
    return f"{namespace}.{escape(cls)}"


def export_escaped_objects(js):
    # flatc's namespace barrels omit the *T re-export for escaped type names.
    for path in js.rglob("*.ts"):
        source = path.read_text()

        def include_object(match):
            cls, module = match.groups()
            definition = path.parent / (module[:-3] + ".ts")
            if cls[:-1] in TS_KEYWORDS and f"export class {cls}T " in definition.read_text():
                return f"export {{ {cls}, {cls}T }} from '{module}';"
            return match[0]

        patched = re.sub(r"export \{ (\w+_) \} from '(\./[^']+\.js)';",
                         include_object, source)
        if patched != source:
            path.write_text(patched)


def union_rules(schema):
    objects = schema["objects"]
    unions, children = {}, {}
    for index, obj in enumerate(objects):
        if obj.get("is_struct"):
            continue
        fields = obj.get("fields", [])
        by_id = {field["id"]: field["name"] for field in fields}
        for field in fields:
            if field.get("deprecated"):
                continue
            spec = field["type"]
            base = spec["base_type"]
            vector = base in ("Vector", "Vector64")
            element = spec.get("element") if vector else base
            member = escape(camel(field["name"]))
            if element == "Union":
                tag = escape(camel(by_id[field["id"] - 1]))
                variants = {}
                for variant in schema["enums"][spec["index"]]["values"]:
                    if not variant["value"]:
                        continue
                    kind = variant["union_type"]
                    variants[variant["value"]] = (
                        "String" if kind["base_type"] == "String"
                        else js_type(objects[kind["index"]]["name"]) + "T"
                    )
                unions.setdefault(index, []).append(
                    (member, tag, vector, field.get("required", False), variants))
            elif element == "Obj":
                children.setdefault(index, []).append((member, vector, spec["index"]))

    # A child only needs visiting if it can reach a union.
    checked = set(unions)
    while True:
        parents = {i for i, rules in children.items()
                   if any(child in checked for _, _, child in rules)}
        if parents <= checked:
            break
        checked.update(parents)

    lines = ["export const outputChecks = new Map(["]
    for index in sorted(checked):
        cls = js_type(objects[index]["name"]) + "T"
        lines.append(f"  [{cls}, {{ unions: [")
        for member, tag, vector, required, variants in unions.get(index, []):
            choices = ", ".join(f"{value}: {kind}" for value, kind in variants.items())
            lines.append(f"    [{json.dumps(member)}, {json.dumps(tag)}, "
                         f"{str(vector).lower()}, {str(required).lower()}, {{ {choices} }}],")
        lines.append("  ], children: [")
        for member, vector, child in children.get(index, []):
            if child in checked:
                target = js_type(objects[child]["name"]) + "T"
                lines.append(f"    [{json.dumps(member)}, {str(vector).lower()}, {target}],")
        lines.append("  ] }],")
    lines.append("]);")
    return lines


def cpp_adapter(contract, names, kinds):
    def cpp_type(port):
        return "::" + "::".join(names.get(part, part) for part in port["type"].split("."))

    lines = [
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        '#include "node_card.hh"',
        "",
        "namespace {",
        "using namespace tsugumi::abi9;",
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  Input input(bytes);",
    ]
    for port in contract["inputs"]:
        name = json.dumps(port["name"])
        kind = kinds[port["kind"]]
        required = str(port["required"]).lower()
        lines.append(
            f"  input.ReadPort<{cpp_type(port)}>({name}, PortKind::{kind}, {required});"
        )
    lines += [
        "  input.CheckConsumed();",
        '  ReadValue<::parameters::Params>(input.Params(), "params");',
        "  auto result = RunNodeCard(bytes);",
        "  for (const auto& port : result.outputs) {",
    ]
    for port in contract["outputs"]:
        name = json.dumps(port["name"])
        lines += [
            f"    if (port->name == {name}) {{",
            f"      VerifyNodeOutput<{cpp_type(port)}>(*port);",
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
    return "\n".join(lines)


def js_bindings(contract, kinds, schema):
    lines = [
        "import { inputs } from './_inputs.js';",
        "import { outputs } from './_outputs.js';",
        "import { parameters } from './_params.js';",
        "import { PortKind } from './tsugumi/data/port-kind.js';",
        "export { inputs, outputs, parameters };",
        "",
        "export interface CardInput {",
        "  params: parameters.ParamsT;",
        "  ports: {",
    ]
    for port in contract["inputs"]:
        optional = "" if port["required"] else "?"
        lines.append(f"    {json.dumps(port['name'])}{optional}: {js_type(port['type'])}T;")
    lines += ["  };", "}", "", "export interface CardOutput {"]
    for port in contract["outputs"]:
        optional = "" if port["required"] else "?"
        lines.append(f"  {json.dumps(port['name'])}{optional}: {js_type(port['type'])}T;")
    lines += ["}", "", "export const readParams = parameters.Params.getRootAsParams;"]
    for side, variable in (("inputs", "inputPorts"), ("outputs", "outputPorts")):
        lines.append(f"export const {variable} = [")
        for port in contract[side]:
            cls = js_type(port["type"])
            table = port["type"].rsplit(".", 1)[-1]
            name = json.dumps(port["name"])
            entry = (f"read: {cls}.{camel('get_root_as_' + table)}" if side == "inputs"
                     else f"object: {cls}T, kind: PortKind.{kinds[port['kind']]}, "
                          f"required: {str(port['required']).lower()}")
            lines.append(f"  {{ name: {name}, {entry} }},")
        lines.append("];")
    return "\n".join([*lines, "", *union_rules(schema)]) + "\n"


if __name__ == "__main__":
    source, cpp, reflection, native, js = map(Path, sys.argv[1:])
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(cpp))
    from card_cpp import KINDS, generate_headers

    contract = json.loads(source.joinpath("_contract.json").read_text())
    native.joinpath("include").mkdir(parents=True)
    names, _ = generate_headers(source, native, reflection)
    native.joinpath("card_adapter.cc").write_text(cpp_adapter(contract, names, KINDS))
    subprocess.run(["flatc", "-b", "--schema", "--bfbs-builtins",
                    "-I", str(source), str(source / "_outputs.fbs")], check=True)
    subprocess.run(["flatc", "-t", "--strict-json", "--defaults-json", str(reflection), "--",
                    "_outputs.bfbs"], check=True)
    schema = json.loads(Path("_outputs.json").read_text())
    export_escaped_objects(js)
    js.joinpath("bindings.ts").write_text(js_bindings(contract, KINDS, schema))
