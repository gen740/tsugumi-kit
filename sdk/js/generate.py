"""Generate the JS Card bindings and the native wire verifier at build time."""

import json
import re
import sys
from pathlib import Path

if sys.argv[1] == "--params":
    values = json.loads(Path(sys.argv[2]).read_text())

    def js_value(value):
        if type(value) is int:
            return f"{value}n"
        if isinstance(value, list):
            return "Object.freeze([" + ", ".join(map(js_value, value)) + "])"
        return json.dumps(value)

    fields = (f"[{json.dumps(name)}]: {js_value(value)}" for name, value in values.items())
    Path(sys.argv[3]).write_text("export const params = Object.freeze({" + ", ".join(fields) + "});\n")
    sys.exit(0)

from tsugumi_ports import load_card_schemas, reflect_schema, write_api_readme


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


def js_type(name, *, output=False):
    namespace, _, cls = name.rpartition(".")
    if (namespace == "tsugumi" or namespace.startswith("tsugumi.")):
        return name
    if output:
        namespace = "outputs" if namespace == "outputs" else ".".join(
            filter(None, ("outputs", namespace.removeprefix("outputs."))))
    return ".".join(filter(None, (namespace, escape(cls))))


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


def link_namespace_barrels(js):
    # flatc writes no barrel for a namespace that holds only namespaces, such
    # as inputs.<port> above a Merge's tsugumi.merge. Export every child.
    for directory in sorted((path for path in js.rglob("*") if path.is_dir()),
                            key=lambda path: -len(path.parts)):
        barrel = directory.with_suffix(".ts")
        source = barrel.read_text() if barrel.exists() else ""
        lines = [f"export * as {child.name} from './{directory.name}/{child.name}.js';"
                 for child in sorted(directory.iterdir()) if child.is_dir()
                 and f"export * as {child.name} from" not in source]
        if lines:
            barrel.write_text(source + "\n".join(lines) + "\n")


def union_rules(schema):
    """Union tag checks for user tables, which the TS object API does not make."""
    objects = schema["objects"]
    unions, children = {}, {}
    for index, obj in enumerate(objects):
        if obj.get("is_struct"):
            continue
        fields = obj.get("fields", [])
        by_id = {field.get("id", 0): field["name"] for field in fields}
        for field in fields:
            if field.get("deprecated"):
                continue
            spec = field["type"]
            base = spec["base_type"]
            vector = base in ("Vector", "Vector64")
            element = spec.get("element") if vector else base
            member = escape(camel(field["name"]))
            if element == "Union":
                tag = escape(camel(by_id[field.get("id", 0) - 1]))
                variants = {}
                for variant in schema["enums"][spec["index"]]["values"]:
                    if not variant.get("value", 0):
                        continue
                    kind = variant["union_type"]
                    variants[variant["value"]] = (
                        "String" if kind["base_type"] == "String"
                        else js_type(objects[kind["index"]]["name"], output=True) + "T"
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

    lines = ["export const outputChecks = new Map<unknown, any>(["]
    for index in sorted(checked):
        cls = js_type(objects[index]["name"], output=True) + "T"
        lines.append(f"  [{cls}, {{ unions: [")
        for member, tag, vector, required, variants in unions.get(index, []):
            choices = ", ".join(f"{value}: {kind}" for value, kind in variants.items())
            lines.append(f"    [{json.dumps(member)}, {json.dumps(tag)}, "
                         f"{str(vector).lower()}, {str(required).lower()}, {{ {choices} }}],")
        lines.append("  ], children: [")
        for member, vector, child in children.get(index, []):
            if child in checked:
                target = js_type(objects[child]["name"], output=True) + "T"
                lines.append(f"    [{json.dumps(member)}, {str(vector).lower()}, {target}],")
        lines.append("  ] }],")
    lines.append("]);")
    return lines


def ts_type(port, *, output=False):
    types = [candidate.type for candidate in port.candidates] or [port.type]
    return " | ".join(js_type(type_, output=output) + "T" for type_ in types)


def descriptor(type_, *, output=False):
    cls = js_type(type_, output=output)
    table = type_.rsplit(".", 1)[-1]
    return f"{{ read: {cls}.{camel('get_root_as_' + table)}, object: {cls}T }}"


def port_descriptor(port, *, output=False):
    name = f"name: {json.dumps(port.name)}"
    if output:
        entries = [name, f"required: {str(port.required).lower()}",
                   f"type: {descriptor(port.type, output=True)}"]
    else:
        candidates = ", ".join(f"[{json.dumps(c.schema)}, {descriptor(c.type)}]"
                               for c in port.candidates)
        entries = [name, f"candidates: new Map([{candidates}])"]
    return "  { " + ", ".join(entries) + " },"


def param_type(type_):
    if type_.startswith("["):
        return f"readonly {param_type(type_[1:-1])}[]"
    return {"bool": "boolean", "long": "bigint", "double": "number", "string": "string"}[type_]


def js_bindings(card, output_schema):
    return "\n".join([
        "import { inputs } from './_inputs.js';",
        "import * as outputs from './output-types.js';",
        "import * as tsugumi from './tsugumi.js';",
        "export { inputs, outputs };",
        "",
        "export interface Params {",
        *(f"  readonly {json.dumps(param.name)}: {param_type(param.type)};" for param in card.params),
        "}",
        "",
        "export interface CardPorts {",
        *(f"  {json.dumps(port.name)}: {ts_type(port)};" for port in card.inputs),
        "}",
        "",
        "export interface CardInput {",
        "  ports: CardPorts;",
        "  need<K extends keyof CardPorts>(name: K): CardPorts[K];",
        "}",
        "",
        "export interface CardOutput {",
        *(f"  {json.dumps(port.name)}{'' if port.required else '?'}: {ts_type(port, output=True)};"
          for port in card.outputs),
        "}",
        "",
        "export const inputPorts = [",
        *map(port_descriptor, card.inputs),
        "];",
        "export const outputPorts = [",
        *(port_descriptor(port, output=True) for port in card.outputs),
        "];",
        "",
        *union_rules(output_schema),
        "",
    ])


def cpp_adapter(card, names, output_schema):
    from card_cpp import cpp_type, validators

    body = [
        "auto RunCard(const tsugumi_call& call) -> ResultBuffer {",
        "  {",
        "    Input input(call);",
    ]
    for port in card.inputs:
        name = json.dumps(port.name)
        body.extend([f"    if (const auto entry = input.ReadPort({name})) {{",
                     "      const Buffer payload(entry->bytes);"])
        for index, member in enumerate(port.candidates):
            keyword = "if" if index == 0 else "else if"
            legacy = "entry->schema.data() == nullptr || " if len(port.candidates) == 1 else ""
            body += [
                f'      {keyword} ({legacy}entry->schema == "{member.schema}") {{',
                f"        if (!payload.Verify<{cpp_type(member.type, names)}>())",
                f'          Reject(FailureKind::input_invalid, "Invalid FlatBuffer for " + std::string({name}));',
                "      }",
            ]
        body += [
            f'      else Reject(FailureKind::input_invalid, "Unknown schema for " + std::string({name}));',
            '    } else {',
            f'      Reject(FailureKind::input_missing, "Missing input " + std::string({name}));',
            '    }',
        ]
    body += [
        "    input.CheckConsumed();",
        "  }",
        "  auto bytes_out = RunNodeCard({call.inputs, call.input_count});",
        "  const Buffer envelope(bytes_out.bytes());",
        "  const auto* result = envelope.Verify<Tsugumi::Card::CardResult>();",
        '  if (!result) Reject(FailureKind::output_invalid, "Invalid Node.js CardResult");',
        "  if (result->failure()) return bytes_out;",
        "  const auto* ports = result->outputs();",
        "  std::unordered_set<std::string> seen;",
        "  if (ports) for (const auto* port : *ports) {",
        "    const auto name = port->name()->str();",
        "    if (!seen.insert(name).second)",
        '      Reject(FailureKind::output_invalid, "Duplicate output " + name);',
    ]
    for port in card.outputs:
        table = cpp_type(port.type, names)
        body += [
            f"    if (name == {json.dumps(port.name)}) {{",
            "      const auto* bytes = port->bytes();",
            "      const Buffer buffer(bytes ? tsugumi_bytes{bytes->data(), bytes->size()}",
            "                                : tsugumi_bytes{});",
            f"      const auto* value = buffer.Verify<{table}>();",
            f"      {table}::NativeTableType native;",
            "      if (value) value->UnPackTo(&native);",
            "      if (!value || !ValidOutput(native))",
            '        Reject(FailureKind::output_invalid, "Invalid output " + name);',
            "      continue;",
            "    }",
        ]
    body += [
        '    Reject(FailureKind::output_invalid, "Unknown output " + name);',
        "  }",
    ]
    for port in card.outputs:
        if port.required:
            name = json.dumps(port.name)
            body += [f"  if (!seen.contains({name}))",
                     f'    Reject(FailureKind::output_invalid, "Missing output " + std::string({name}));']
    body += ["  return bytes_out;", "}"]
    return "\n".join([
        '#include <string>',
        '#include <unordered_set>',
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "node_card.hh"',
        "",
        "namespace {",
        "using namespace tsugumi::abi;",
        "",
        *validators(output_schema, (cpp_type(port.type, names) for port in card.outputs)),
        *body,
        "}  // namespace",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi::Invoke(call, result, RunCard);",
        "}",
        "",
    ])


def write_readme(card, path):
    write_api_readme(
        path,
        ((port.name, f"{ts_type(port)} | undefined") for port in card.inputs),
        ((port.name, ts_type(port, output=True) + (" (required output)" if port.required
                                      else " | undefined")) for port in card.outputs),
        ((param.name, param.type) for param in card.params),
    )


if __name__ == "__main__":
    schemas, cpp, reflection, native, js, readme = map(Path, sys.argv[1:])
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(cpp))
    from card_cpp import generate_headers

    card = load_card_schemas(schemas, reflection)
    native.joinpath("include").mkdir(parents=True)
    names, cpp_output_schema = generate_headers(schemas, native, reflection)
    native.joinpath("card_adapter.cc").write_text(cpp_adapter(card, names, cpp_output_schema))
    export_escaped_objects(js)
    link_namespace_barrels(js)
    output_schema = reflect_schema(schemas / "_outputs.fbs", reflection)
    output_names = [item["name"] for item in output_schema["objects"] + output_schema["enums"]]
    exports = ["export * from './_outputs.js';"]
    if any(name.startswith("outputs.") for name in output_names):
        exports.append("export * from './outputs.js';")
    (js / "output-types.ts").write_text("\n".join(exports) + "\n")
    js.joinpath("bindings.ts").write_text(
        js_bindings(card, output_schema))
    write_readme(card, readme)
