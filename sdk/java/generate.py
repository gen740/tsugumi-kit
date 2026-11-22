"""Generate Java records, a typed dispatch and native validators for a Card."""

import json
import re
import subprocess
import sys
from pathlib import Path

JAVA_RESERVED = set("""
abstract assert boolean break byte case catch char class const continue default
do double else enum extends final finally float for goto if implements import
instanceof int interface long native new package private protected public return
short static strictfp super switch synchronized this throw throws transient try
void volatile while true false null _ var yield record sealed permits
""".split())
RECORD_RESERVED = set("clone finalize getClass hashCode notify notifyAll toString wait".split())
KINDS = {"file": "File", "directory": "Directory", "flatbuffer": "FlatBuffer"}
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


def java_schemas(source, target, cpp):
    schemas = {file.relative_to(source): file.read_text(encoding="utf-8")
               for file in source.rglob("*.fbs") if not file.name.startswith("._")}
    tokens = {path: cpp.schema_tokens(text) for path, text in schemas.items()}
    identifiers = {token.group() for matches in tokens.values() for token in matches
                   if IDENTIFIER.fullmatch(token.group())}
    names = {}
    for name in sorted(identifiers & (JAVA_RESERVED | RECORD_RESERVED)):
        replacement = name + "_"
        while replacement in identifiers:
            replacement += "_"
        names[name] = replacement
        identifiers.add(replacement)

    def rewrite(text, matches):
        # The Java generator does not escape package declarations, and its
        # suffix can collide with an existing name. Rewrite identifiers before
        # flatc sees them, while retaining field order, values and layout.
        pieces = []
        offset = 0
        previous = before_previous = ""
        for index, token in enumerate(matches):
            word = token.group()
            following = matches[index + 1].group() if index + 1 < len(matches) else ""
            replacement = word
            if (before_previous, previous) == ("nested_flatbuffer", ":") and word.startswith('"'):
                qualified = ".".join(names.get(part, part) for part in word[1:-1].split("."))
                replacement = '"' + qualified + '"'
            elif word in names:
                unqualified = previous != "." and following != "."
                declaration = word == "enum" and previous in {"", ";", "}"}
                scalar = (word in {"byte", "short", "int", "long", "float", "double"}
                          and previous in {":", "[", "("} and following != ":" and unqualified)
                literal = word in {"true", "false", "null"} and previous == "=" and unqualified
                if not (declaration or scalar or literal):
                    replacement = names[word]
            if replacement != word:
                pieces.extend((text[offset:token.start()], replacement))
                offset = token.end()
            before_previous, previous = previous, word
        pieces.append(text[offset:])
        return "".join(pieces)

    for path, text in schemas.items():
        output = target / path
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rewrite(text, tokens[path]), encoding="utf-8")
    return names


def members(entries, names, reserved=()):
    used = set(reserved) | RECORD_RESERVED
    for port in entries:
        name = port["name"]
        member = names.get(name, name)
        while member in used:
            member += "_"
        used.add(member)
        schema_type = ".".join(names.get(part, part) for part in port["type"].split("."))
        yield port, member, schema_type


def signature(fields):
    return ",\n".join(f"            {value_type} {name}" for value_type, name in fields)


def union_checks(schema, roots):
    """Validate object-API unions before flatc's pack can discard a value."""
    objects, enums = schema["objects"], schema["enums"]
    functions = {}
    contained = {}
    for index, obj in enumerate(objects):
        contained[index] = members = []
        for field in obj["fields"]:
            spec = field["type"]
            vector = spec["base_type"] in {"Vector", "Vector64", "Array"}
            base = spec["element"] if vector else spec["base_type"]
            if not field["deprecated"] and base in {"Obj", "Union"}:
                members.append((field, vector, base, spec["index"]))
    checked = {index for index, members in contained.items()
               if any(base == "Union" for _, _, base, _ in members)}
    while True:
        parents = {index for index, members in contained.items()
                   if any(base == "Obj" and child in checked for _, _, base, child in members)}
        if parents <= checked:
            break
        checked.update(parents)

    def check_union(index):
        enum = enums[index]
        name = enum["name"]
        signature = f"check({name}Union value, boolean required)"
        if signature in functions:
            return
        error = f'throw new IllegalArgumentException("Invalid {name} union");'
        functions[signature] = body = [
            "        if (value == null || value.getType() == 0) {",
            f"            if (required || (value != null && value.getValue() != null)) {error}",
            "            return;",
            "        }",
            "        switch (value.getType()) {",
        ]
        for variant in enum["values"]:
            if variant["value"] == 0:
                continue
            target = variant["union_type"]
            target_type = "String"
            recurse = target["base_type"] == "Obj" and target["index"] in checked
            if target["base_type"] == "Obj":
                target_type = objects[target["index"]]["name"] + "T"
            if recurse:
                check_table(target["index"])
            body.append(f"            case {name}.{variant['name']} -> {{")
            body.append(f"                if (!(value.getValue() instanceof {target_type} child)) {error}")
            if recurse:
                body.append("                check(child);")
            body.append("            }")
        body += [f"            default -> {error}", "        }"]

    def check_table(index):
        obj = objects[index]
        signature = f"check({obj['name']}T value)"
        if signature in functions:
            return
        functions[signature] = body = []
        if index not in checked:
            return
        body.append("        if (value == null) return;")
        for field, vector, base, child in contained[index]:
            if base == "Obj" and child not in checked:
                continue
            # flatc builds the getter from the lower camel case of get_<field>.
            getter = "get" + re.sub(r"_(.)", lambda m: m[1].upper(), "_" + field["name"])
            call = "child" if vector else f"value.{getter}()"
            if base == "Obj":
                check_table(child)
            else:
                check_union(child)
                required = str(vector or field["required"]).lower()
                call += f", {required}"
            if vector:
                body.append(f"        if (value.{getter}() != null) {{")
                body.append(f"            for (var child : value.{getter}()) check({call});")
                body.append("        }")
            else:
                body.append(f"        check({call});")

    by_name = {obj["name"]: index for index, obj in enumerate(objects)}
    for root in roots:
        check_table(by_name[root])
    return "\n\n".join(f"    static void {signature} {{\n"
                       + "\n".join(body) + "\n    }"
                       for signature, body in functions.items())


def generate_java(contract, names, src, entry_class, out, enum_schema, output_schema):
    inputs = list(members(contract["inputs"], names, {"params"}))
    outputs = list(members(contract["outputs"], names))
    typed = lambda port, schema: schema + "T" if port["kind"] == "flatbuffer" else "Path"
    input_fields = [("parameters.ParamsT", "params")] + [
        (typed(port, schema), member) for port, member, schema in inputs
    ]
    output_fields = [(typed(port, schema), member) for port, member, schema in outputs]
    out.mkdir(parents=True, exist_ok=True)
    (out / "Card.java").write_text(f"""// Generated by the Tsugumi Java SDK.
package tsugumi;

import java.nio.file.Path;

public final class Card {{
    private Card() {{}}

    public record Input(
{signature(input_fields)}) {{}}

    public record Output(
{signature(output_fields)}) {{}}
}}
""", encoding="utf-8")

    reads = []
    for port, _, schema in inputs:
        root = schema.rsplit(".", 1)[-1]
        decode = (f"buffer -> {schema}.getRootAs{root}(buffer).unpack()"
                  if port["kind"] == "flatbuffer"
                  else f"buffer -> Path.of({schema}.getRootAs{root}(buffer).value())")
        reads.append(f"ports.read({json.dumps(port['name'])}, {decode})")

    writes = []
    for port, member, schema in outputs:
        kind = KINDS[port["kind"]]
        root = schema.rsplit(".", 1)[-1]
        if kind == "FlatBuffer":
            pack = (f"(builder, port) -> {{ UnionChecks.check(port); "
                    f"return {schema}.pack(builder, port); }}")
        else:
            pack = f"(builder, path) -> {schema}.create{root}(builder, builder.createString(path.toString()))"
        writes.append(f"result.add({json.dumps(port['name'])}, PortKind.{kind}, value.{member}(), {pack});")

    sources = sorted(path.relative_to(src).as_posix() for path in src.rglob("*.java")
                     if not path.name.startswith("._"))
    source_args = ", ".join(json.dumps(path, ensure_ascii=False) for path in sources)
    read_args = ",\n                    ".join(["ports.params()", *reads])
    write_code = "\n            ".join(writes)
    (out / "Entry.java").write_text(f"""// Generated by the Tsugumi Java SDK.
package tsugumi;

import java.nio.file.Path;
import java.util.Set;
import tsugumi.wire.Tsugumi.Data.PortKind;

final class Entry {{
    private static final Set<String> SOURCES = Set.of({source_args});

    static byte[] invoke(byte[] bytes) {{
        try {{
            var ports = new Wire.Inputs(bytes);
            var input = new Card.Input(
                    {read_args});
            Card.Output value = {entry_class}.run(input);
            if (value == null) {{
                throw new Failure(FailureKind.OUTPUT_INVALID, "run returned null");
            }}
            var result = new Wire.Outputs();
            {write_code}
            return result.finish();
        }} catch (Throwable error) {{
            return Wire.failure(error, SOURCES);
        }}
    }}
}}
""", encoding="utf-8")

    checks = union_checks(output_schema, (
        schema for port, _, schema in outputs if port["kind"] == "flatbuffer"
    ))
    (out / "UnionChecks.java").write_text(f"""// Generated by the Tsugumi Java SDK.
package tsugumi;

final class UnionChecks {{
    private UnionChecks() {{}}

{checks}
}}
""", encoding="utf-8")

    enum_body = re.search(r"enum FailureKind\s*:\s*ushort\s*\{(.*?)\}", enum_schema, re.S)[1]
    kinds = re.findall(r"^\s*(\w+)\s*=\s*\d+", enum_body, re.M)
    constants = ",\n".join(
        f"    {kind.upper()}(tsugumi.wire.Tsugumi.Data.FailureKind.{kind})"
        for kind in kinds if kind != "unspecified"
    )
    (out / "FailureKind.java").write_text(f"""// Generated from the Card ABI.
package tsugumi;

public enum FailureKind {{
{constants};

    final int value;

    FailureKind(int value) {{
        this.value = value;
    }}
}}
""", encoding="utf-8")


def generate_native(contract, names, out):
    def ports(side):
        return "\n".join(
            f"    {{{json.dumps(port['name'])}, PortKind::{KINDS[port['kind']]}, "
            f"{str(port['required']).lower()}, VerifyJavaTable<::{schema.replace('.', '::')}>}},"
            for port, _, schema in members(contract[side], names)
        )

    (out / "card_adapter.cc").write_text(f"""// Generated by the Tsugumi Java SDK.
#include <array>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "host.hh"

namespace {{
using namespace tsugumi::abi9;

constexpr std::array<JavaPort, {len(contract["inputs"])}> inputs{{{{
{ports("inputs")}
}}}};
constexpr std::array<JavaPort, {len(contract["outputs"])}> outputs{{{{
{ports("outputs")}
}}}};

auto Run(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {{
  return RunJava(bytes, VerifyJavaTable<::parameters::Params>, inputs, outputs);
}}
}}  // namespace

extern "C" __attribute__((visibility("default")))
auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {{
  return tsugumi::abi9::Invoke(call, result, Run);
}}
""", encoding="utf-8")


def main(cpp_dir, contract_dir, src, entry_class, out, reflection, enums):
    sys.dont_write_bytecode = True
    sys.path.insert(0, cpp_dir)
    import card_cpp

    if not re.fullmatch(r"(?:[A-Za-z_][A-Za-z_0-9]*\.)+[A-Za-z_][A-Za-z_0-9]*", entry_class):
        raise ValueError("javaCard: mainClass must be a package-qualified Java class")
    contract_dir, src, out = map(Path, (contract_dir, src, out))
    contract = json.loads((contract_dir / "_contract.json").read_text(encoding="utf-8"))
    names = java_schemas(contract_dir, out / "schemas", card_cpp)
    for schema in card_cpp.SCHEMAS:
        subprocess.run(["flatc", "--java", "--gen-object-api", "--gen-all",
                        "-o", str((out / "java").resolve()), f"{schema}.fbs"],
                       cwd=out / "schemas", check=True)
    work = (out / "schemas").resolve()
    subprocess.run(["flatc", "--binary", "--schema", "_outputs.fbs"], cwd=work, check=True)
    subprocess.run(["flatc", "--json", "--strict-json", "--defaults-json", "--raw-binary",
                    reflection, "--", "_outputs.bfbs"], cwd=work, check=True)
    generate_java(contract, names, src, entry_class, out / "java" / "tsugumi",
                  Path(enums).read_text(encoding="utf-8"),
                  json.loads((work / "_outputs.json").read_text(encoding="utf-8")))
    cpp_names, _ = card_cpp.generate_headers(contract_dir, out.resolve(), reflection)
    generate_native(contract, cpp_names, out)


if __name__ == "__main__":
    main(*sys.argv[1:])
