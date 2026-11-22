"""Generate the JS Card bindings and the native wire verifier at build time."""

import json
import re
import sys
from pathlib import Path

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

    lines = ["export const outputChecks = new Map<unknown, any>(["]
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


def ts_value_type(type_, members):
    if members:
        return " | ".join(ts_value_type(member.type, member.merge) for member in members)
    return js_type(type_) + "T"


def ts_type(port):
    return "string" if port.kind == "Path" else ts_value_type(port.type, port.merge)


def descriptor(type_, members):
    """The runtime's view of one FlatBuffer type, with a Merge's boxed members."""
    cls = js_type(type_)
    table = type_.rsplit(".", 1)[-1]
    entries = [f"read: {cls}.{camel('get_root_as_' + table)}", f"object: {cls}T"]
    if members:
        boxed = ", ".join(
            f"{{ tag: {member.value}, box: {js_type(member.box_type)}, "
            f"boxObject: {js_type(member.box_type)}T, "
            f"type: {descriptor(member.type, member.merge)} }}"
            for member in members)
        entries.append(f"members: [{boxed}]")
    return "{ " + ", ".join(entries) + " }"


def port_descriptor(port):
    entries = [f"name: {json.dumps(port.name)}", f"kind: {json.dumps(port.kind)}",
               f"required: {str(port.required).lower()}"]
    if port.kind == "FlatBuffer":
        entries.append(f"type: {descriptor(port.type, port.merge)}")
    return "  { " + ", ".join(entries) + " },"


def js_bindings(card, output_schema):
    lines = [
        "import { inputs } from './_inputs.js';",
        "import { outputs } from './_outputs.js';",
        "import { parameters } from './_params.js';",
        "export { inputs, outputs, parameters };",
        "",
        "// Connected inputs; any of them may be absent from a call.",
        "export interface CardPorts {",
    ]
    lines += [f"  {json.dumps(port.name)}?: {ts_type(port)};" for port in card.inputs]
    lines += [
        "}",
        "",
        "export interface CardInput {",
        f"  params: {js_type(card.params_type)}T;",
        "  ports: CardPorts;",
        "  // The input's value, or an input_missing Failure naming the port.",
        "  need<K extends keyof CardPorts>(name: K): Exclude<CardPorts[K], undefined>;",
        "  need(name: string): Exclude<CardPorts[keyof CardPorts], undefined>;",
        "}",
        "",
        "export interface CardOutput {",
    ]
    for port in card.outputs:
        optional = "" if port.required else "?"
        suffix = "" if port.required else " | undefined"
        lines.append(f"  {json.dumps(port.name)}{optional}: {ts_type(port)}{suffix};")
    table = card.params_type.rsplit(".", 1)[-1]
    lines += [
        "}",
        "",
        f"export const readParams = {js_type(card.params_type)}.{camel('get_root_as_' + table)};",
        "export const inputPorts = [",
        *map(port_descriptor, card.inputs),
        "];",
        "export const outputPorts = [",
        *map(port_descriptor, card.outputs),
        "];",
        "",
        *union_rules(output_schema),
    ]
    return "\n".join(lines) + "\n"


def cpp_adapter(card, names):
    # The C++ generator's renamed identifiers, shared with its headers.
    from card_cpp import cpp_type, flatc_identifier

    functions = {}

    def verify_value(type_, members, bytes_, name, kind):
        merge = ", " + verify_merge(type_, members) if members else ""
        return (f"VerifyNodeValue<{cpp_type(type_, names)}>({bytes_}, "
                f"{name}, {kind}{merge});")

    def verify_merge(type_, members):
        function = f"VerifyMerge{len(functions)}"
        functions[function] = body = [
            f"auto {function}(const {cpp_type(type_, names)}& merged,",
            "    std::string_view name, FailureKind kind) -> void {",
        ]
        for member in members:
            tag = flatc_identifier(names.get(member.tag, member.tag))
            body += [
                f"  if (const auto* box = merged.value_as_{tag}()) {{",
                "    const auto* bytes = box->bytes();",
                "    if (!bytes) Reject(kind, \"Missing Merge bytes for \" + std::string(name));",
                "    " + verify_value(member.type, member.merge,
                                      "{bytes->data(), bytes->size()}", "name", "kind"),
                "    return;",
                "  }",
            ]
        body += ['  Reject(kind, "Invalid Merge port " + std::string(name));', "}", ""]
        return function

    body = [
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  {",
        "    Input input(bytes);",
    ]
    for port in card.inputs:
        name = json.dumps(port.name)
        if port.kind == "Path":
            body.append(f"    input.ReadPath({name});")
        else:
            body += [
                f"    if (const auto payload = input.ReadPortBytes({name}))",
                "      " + verify_value(port.type, port.merge, "*payload", name,
                                        "FailureKind::invalid_request"),
            ]
    body += [
        "    input.CheckConsumed();",
        "    " + verify_value(card.params_type, (), "input.Params()", '"params"',
                              "FailureKind::invalid_request"),
        "  }",
        "  auto result = RunNodeCard(bytes);",
        "  std::unordered_set<std::string> seen;",
        "  for (const auto& port : result.outputs) {",
        "    if (!seen.insert(port->name).second)",
        '      Reject(FailureKind::output_invalid, "Duplicate output " + port->name);',
    ]
    for port in card.outputs:
        body.append(f"    if (port->name == {json.dumps(port.name)}) {{")
        if port.kind == "Path":
            body.append("      VerifyPathOutput(*port);")
        else:
            body += [
                "      VerifyFlatBufferOutput(*port);",
                "      " + verify_value(port.type, port.merge,
                                        "{port->bytes.data(), port->bytes.size()}",
                                        "port->name", "FailureKind::output_invalid"),
            ]
        body += ["      continue;", "    }"]
    body += [
        '    Reject(FailureKind::output_invalid, "Unknown output " + port->name);',
        "  }",
    ]
    for port in card.outputs:
        if port.required:
            name = json.dumps(port.name)
            body += [f"  if (!result.failure && !seen.contains({name}))",
                     f'    Reject(FailureKind::output_invalid, "Missing output " + std::string({name}));']
    body += ["  return result;", "}"]
    return "\n".join([
        '#include <string>',
        '#include <string_view>',
        '#include <unordered_set>',
        "",
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        '#include "node_card.hh"',
        "",
        "namespace {",
        "using namespace tsugumi::abi;",
        "",
        *(line for function in reversed(functions.values()) for line in function),
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
        ((port.name, ts_type(port) + (" (required output)" if port.required
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
    names, _ = generate_headers(schemas, native, reflection)
    native.joinpath("card_adapter.cc").write_text(cpp_adapter(card, names))
    export_escaped_objects(js)
    link_namespace_barrels(js)
    js.joinpath("bindings.ts").write_text(
        js_bindings(card, reflect_schema(schemas / "_outputs.fbs", reflection)))
    write_readme(card, readme)
