"""Generate the Java Card API, a typed dispatch and native validators for a Card."""

import json
import re
import subprocess
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, reflect_schema, write_api_readme

JAVA_RESERVED = set("""
abstract assert boolean break byte case catch char class const continue default
do double else enum extends final finally float for goto if implements import
instanceof int interface long native new package private protected public return
short static strictfp super switch synchronized this throw throws transient try
void volatile while true false null _ var yield record sealed permits
""".split())
RECORD_RESERVED = set("clone finalize getClass hashCode notify notifyAll toString wait".split())
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


def union_checks(schema, roots):
    """Validate object-API unions before flatc's pack can discard a value."""
    objects, enums = schema["objects"], schema["enums"]
    functions = {}
    contained = {}
    for index, obj in enumerate(objects):
        contained[index] = members = []
        for field in obj.get("fields", []):
            spec = field["type"]
            vector = spec.get("base_type") in {"Vector", "Vector64", "Array"}
            base = spec.get("element") if vector else spec.get("base_type")
            if not field.get("deprecated", False) and base in {"Obj", "Union"}:
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
            if variant.get("value", 0) == 0:
                continue
            target = variant["union_type"]
            target_type = "String"
            recurse = target.get("base_type") == "Obj" and target["index"] in checked
            if target.get("base_type") == "Obj":
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
                required = str(vector or field.get("required", False)).lower()
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

class Generator:
    def __init__(self, card, names):
        self.card = card
        self.names = names
        self.functions = []

    def java(self, name):
        return ".".join(self.names.get(part, part) for part in name.split("."))

    def port_type(self, port):
        if port.kind == "Path":
            return "Path"
        return "Object" if port.merge else self.java(port.type) + "T"

    def members(self, entries, reserved):
        used = set(reserved) | RECORD_RESERVED
        for port in entries:
            member = self.names.get(port.name, port.name)
            while member in used:
                member += "_"
            used.add(member)
            yield port, member

    def unpack(self, type_, buffer):
        table = self.java(type_)
        return f"{table}.getRootAs{table.rsplit('.', 1)[-1]}({buffer}).unpack()"

    def decode_merge(self, type_, members):
        function = f"decodeMerge{len(self.functions)}"
        merged = self.java(type_)
        self.functions.append(body := [
            f"    static Object {function}({merged} merged, String name) {{",
            "        switch (Byte.toUnsignedInt(merged.valueType())) {",
        ])
        for member in members:
            box = self.java(member.box_type)
            value = self.unpack(member.type, "bytes")
            if member.merge:
                nested = self.decode_merge(member.type, member.merge)
                table = self.java(member.type)
                value = f"{nested}({table}.getRootAs{table.rsplit('.', 1)[-1]}(bytes), name)"
            body += [
                f"            case {member.value} -> {{",
                f"                var box = ({box}) merged.value(new {box}());",
                "                var nested = box == null ? null : box.bytesAsByteBuffer();",
                "                if (nested != null) {",
                "                    var bytes = nested.slice();",
                f"                    return {value};",
                "                }",
                "            }",
            ]
        body += [
            "            default -> {}",
            "        }",
            '        throw new Failure(FailureKind.INVALID_REQUEST, "Invalid Merge input " + name);',
            "    }",
        ]
        return function

    def encode_merge(self, type_, members):
        function = f"encodeMerge{len(self.functions)}"
        merged = self.java(type_)
        self.functions.append(body := [
            f"    static byte[] {function}(Object value, String name) {{",
            "        byte[] member = null;",
            "        int tag = 0;",
        ])
        branch = "if"
        for member in members:
            table = self.java(member.type)
            tag = member.value
            if member.merge:
                nested = self.encode_merge(member.type, member.merge)
                body += [f"        {branch} ((member = {nested}(value, name)) != null) {{",
                         f"            tag = {tag};", "        }"]
            else:
                body += [f"        {branch} (value instanceof {table}T item) {{",
                         "            member = Wire.pack(name, builder -> {",
                         "                UnionChecks.check(item);",
                         f"                return {table}.pack(builder, item);",
                         "            });",
                         f"            tag = {tag};", "        }"]
            branch = "else if"
        body += [
            "        if (member == null) return null;",
            "        var boxed = new FlatBufferBuilder();",
            "        int box = switch (tag) {",
        ]
        for member in members:
            box = self.java(member.box_type)
            short = box.rsplit(".", 1)[-1]
            body.append(f"            case {member.value} -> "
                        f"{box}.create{short}(boxed, {box}.createBytesVector(boxed, member));")
        body += [
            "            default -> throw new IllegalStateException();",
            "        };",
            f"        boxed.finish({merged}.create{merged.rsplit('.', 1)[-1]}(boxed, (byte) tag, box));",
            "        return boxed.sizedByteArray();",
            "    }",
        ]
        return function

    def generate(self, src, entry_class, out, enum_schema, output_schema):
        card = self.card
        inputs = list(self.members(card.inputs, {"params", "get", "find"}))
        outputs = list(self.members(card.outputs, ()))
        params = self.java(card.params_type)
        accessors = "\n".join(
            f"        public Optional<{self.port_type(port)}> {member}() {{\n"
            f"            return find({json.dumps(port.name)}, {self.port_type(port)}.class);\n"
            "        }\n"
            for port, member in inputs
        )
        output_fields = ",\n".join(f"            {self.port_type(port)} {member}"
                                   for port, member in outputs)
        out.mkdir(parents=True, exist_ok=True)
        (out / "Card.java").write_text(f"""// Generated by the Tsugumi Java SDK. Do not edit.
package tsugumi;

import java.nio.file.Path;
import java.util.Map;
import java.util.Optional;

public final class Card {{
    private Card() {{}}

    /** The Card's inputs. Every input may be absent. */
    public static final class Input {{
        private final {params}T params;
        private final Map<String, Object> values;

        Input({params}T params, Map<String, Object> values) {{
            this.params = params;
            this.values = values;
        }}

        public {params}T params() {{
            return params;
        }}

        /** A needed input. Fails the call with INPUT_MISSING when it is absent. */
        public <T> T get(String name, Class<T> type) {{
            return find(name, type).orElseThrow(
                    () -> new Failure(FailureKind.INPUT_MISSING, "Missing input " + name));
        }}

        /** An input that may be absent, including a port that is not wired. */
        public <T> Optional<T> find(String name, Class<T> type) {{
            Object value = values.get(name);
            if (value != null && !type.isInstance(value)) {{
                throw new Failure(FailureKind.INVALID_REQUEST, "Wrong Java type for input " + name);
            }}
            return Optional.ofNullable(type.cast(value));
        }}

{accessors}    }}

    /** The Card's outputs. Pass null to omit an optional output. */
    public record Output(
{output_fields}) {{}}
}}
""", encoding="utf-8")

        reads = []
        for port, _ in inputs:
            name = json.dumps(port.name)
            if port.kind == "Path":
                reads.append(f"ports.path({name}, values);")
                continue
            value = self.unpack(port.type, "buffer")
            if port.merge:
                table = self.java(port.type)
                decode = self.decode_merge(port.type, port.merge)
                value = (f"Ports.{decode}({table}.getRootAs{table.rsplit('.', 1)[-1]}(buffer), {name})")
            reads.append(f"ports.read({name}, buffer -> {value}, values);")
        writes = []
        for port, member in outputs:
            name = json.dumps(port.name)
            value = f"value.{member}()"
            if port.kind == "Path":
                writes.append(f"result.path({name}, {value});")
            elif port.merge:
                encode = self.encode_merge(port.type, port.merge)
                writes += [f"if ({value} != null) {{",
                           f"    byte[] payload = Ports.{encode}({value}, {name});",
                           "    if (payload == null) {",
                           "        throw new Failure(FailureKind.OUTPUT_INVALID, "
                           f'"Invalid Merge output " + {name});',
                           "    }",
                           f"    result.add({name}, payload);",
                           "}"]
            else:
                table = self.java(port.type)
                writes += [f"if ({value} != null) {{",
                           f"    var port = {value};",
                           f"    result.add({name}, Wire.pack({name}, builder -> {{",
                           "        UnionChecks.check(port);",
                           f"        return {table}.pack(builder, port);",
                           "    }));",
                           "}"]

        sources = sorted(path.relative_to(src).as_posix() for path in src.rglob("*.java")
                         if not path.name.startswith("._"))
        source_args = ", ".join(json.dumps(path, ensure_ascii=False) for path in sources)
        read_code = "\n            ".join(reads)
        write_code = "\n            ".join(writes)
        (out / "Entry.java").write_text(f"""// Generated by the Tsugumi Java SDK.
package tsugumi;

import com.google.flatbuffers.FlatBufferBuilder;
import java.util.HashMap;
import java.util.Set;

final class Entry {{
    private static final Set<String> SOURCES = Set.of({source_args});

    static byte[] invoke(byte[] bytes) {{
        try {{
            var ports = new Wire.Inputs(bytes);
            var values = new HashMap<String, Object>();
            {read_code}
            var input = new Card.Input(ports.params(), values);
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

final class Ports {{
    private Ports() {{}}

{chr(10).join(line for function in self.functions for line in function + [""])}}}
""", encoding="utf-8")

        roots = dict.fromkeys(self.java(type_) for port, _ in outputs if port.kind == "FlatBuffer"
                              for type_ in leaves(port.type, port.merge))
        checks = union_checks(output_schema, roots)
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
        return inputs, outputs


def leaves(type_, members):
    if not members:
        yield type_
    for member in members:
        yield from leaves(member.type, member.merge)


def generate_native(card, cpp_names, cpp, out):
    def ports(entries):
        rows = []
        for port in entries:
            verify = ("nullptr" if port.kind == "Path"
                      else f"VerifyJavaTable<{cpp.cpp_type(port.type, cpp_names)}>")
            rows.append(f"    {{{json.dumps(port.name)}, PortKind::{port.kind}, "
                        f"{str(port.required).lower()}, {verify}}},")
        return "\n".join(rows)

    params = cpp.cpp_type(card.params_type, cpp_names)
    (out / "card_adapter.cc").write_text(f"""// Generated by the Tsugumi Java SDK.
#include <array>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "host.hh"

namespace {{
using namespace tsugumi::abi;

constexpr std::array<JavaPort, {len(card.inputs)}> inputs{{{{
{ports(card.inputs)}
}}}};
constexpr std::array<JavaPort, {len(card.outputs)}> outputs{{{{
{ports(card.outputs)}
}}}};

auto Run(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {{
  return RunJava(bytes, VerifyJavaTable<{params}>, inputs, outputs);
}}
}}  // namespace

extern "C" __attribute__((visibility("default")))
auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {{
  return tsugumi::abi::Invoke(call, result, Run);
}}
""", encoding="utf-8")


def main(cpp_dir, schemas, src, entry_class, out, reflection, enums):
    sys.dont_write_bytecode = True
    sys.path.insert(0, cpp_dir)
    import card_cpp

    if not re.fullmatch(r"(?:[A-Za-z_][A-Za-z_0-9]*\.)+[A-Za-z_][A-Za-z_0-9]*", entry_class):
        raise ValueError("javaCard: mainClass must be a package-qualified Java class")
    schemas, src, out = (Path(path).resolve() for path in (schemas, src, out))
    card = load_card_schemas(schemas, reflection)
    names = java_schemas(schemas, out / "schemas", card_cpp)
    work = out / "schemas"
    for schema in card_cpp.SCHEMAS:
        subprocess.run(["flatc", "--java", "--gen-object-api", "--gen-all",
                        "-o", str(out / "java"), f"{schema}.fbs"], cwd=work, check=True)
    generator = Generator(card, names)
    inputs, outputs = generator.generate(
        src, entry_class, out / "java" / "tsugumi", Path(enums).read_text(encoding="utf-8"),
        reflect_schema(work / "_outputs.fbs", reflection))
    cpp_names, _ = card_cpp.generate_headers(schemas, out, reflection)
    generate_native(card, cpp_names, card_cpp, out)

    def described(port):
        if port.merge:
            return "Object (" + " | ".join(generator.java(type_) + "T"
                                           for type_ in leaves(port.type, port.merge)) + ")"
        return generator.port_type(port)

    write_api_readme(
        out / "README.md",
        ((port.name, f"input.{member}(): Optional<{described(port)}>") for port, member in inputs),
        ((port.name, f"Output.{member}: {described(port)}"
          + (" (required)" if port.required else "")) for port, member in outputs),
        ((param.name, param.type) for param in card.params),
    )


if __name__ == "__main__":
    main(*sys.argv[1:])
