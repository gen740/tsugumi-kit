"""Generate the Java Card API, a typed dispatch and native validators for a Card."""

import json
import re
import subprocess
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, param_members, reflect_schema, write_api_readme

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

def java_type(type_):
    if type_.startswith("["):
        boxed = {"bool": "Boolean", "long": "Long", "double": "Double", "string": "String"}
        return f"java.util.List<{boxed[type_[1:-1]]}>"
    return {"bool": "boolean", "long": "long", "double": "double", "string": "String"}[type_]


def java_literal(value, type_):
    if type_.startswith("["):
        members = ", ".join(java_literal(v, type_[1:-1]) for v in value)
        return f"java.util.List.of({members})"
    if type_ == "long":
        return f"{value}L"
    if type_ == "double":
        return repr(float(value))
    if type_ == "string":
        # Java processes Unicode escapes before tokenization. Use octal for
        # ASCII control characters and keep Unicode text in source as UTF-8.
        return '"' + "".join(
            f"\\{ord(char):03o}" if ord(char) < 32 or ord(char) == 127
            else "\\" + char if char in {'"', "\\"} else char
            for char in value
        ) + '"'
    return json.dumps(value)


def param_fields(card):
    return list(zip(card.params, param_members((p.name for p in card.params),
                                              JAVA_RESERVED | RECORD_RESERVED)))


def embed_params(card, values, output):
    literals = []
    for param, _ in param_fields(card):
        if values is None:
            value = [] if param.type.startswith("[") else {
                "bool": False, "long": 0, "double": 0.0, "string": "",
            }[param.type]
        else:
            value = values[param.name]
        literals.append(java_literal(value, param.type))
    arguments = ", ".join(literals)
    output.write_text(f"""// Generated by the Tsugumi Java SDK.
package tsugumi;

final class Parameters {{
    private Parameters() {{}}
    static final Card.Params VALUE = new Card.Params({arguments});
}}
""", encoding="utf-8")


class Generator:
    def __init__(self, card, names, output_package):
        self.card = card
        self.names = names
        self.output_package = output_package

    def java(self, name, *, output=False):
        name = ".".join(self.names.get(part, part) for part in name.split("."))
        if output and name != "tsugumi.Artifact":
            return self.output_package + name
        return name

    def port_type(self, port, *, output=False):
        if len(port.candidates) > 1:
            return f"Card.{self.sum_type(port)}"
        return self.java(port.type, output=output) + "T"

    def sum_type(self, port):
        return "Input_" + port.name

    def members(self, entries, reserved):
        used = set(reserved) | RECORD_RESERVED | JAVA_RESERVED
        for port in entries:
            member = self.names.get(port.name, port.name)
            while member in used:
                member += "_"
            used.add(member)
            yield port, member

    def unpack(self, type_, buffer):
        table = self.java(type_)
        return f"{table}.getRootAs{table.rsplit('.', 1)[-1]}({buffer}).unpack()"

    def generate(self, out, enum_schema, output_schema):
        card = self.card
        inputs = list(self.members(card.inputs, {"get", "find"}))
        outputs = list(self.members(card.outputs, ()))
        variants, reads = [], []
        for port, _ in inputs:
            name = json.dumps(port.name)
            if len(port.candidates) == 1:
                candidate, = port.candidates
                value = self.unpack(candidate.type, f"p_{port.name}.bytes()")
                reads += [f"var p_{port.name} = ports.take({name});",
                          f"values.put({name}, {value});"]
                continue
            sum_type = self.sum_type(port)
            variants += [f"    public sealed interface {sum_type} {{"]
            branches = []
            used = set(JAVA_RESERVED)
            for candidate in port.candidates:
                tag = self.java(candidate.type).rsplit(".", 1)[-1]
                while tag in used or tag == sum_type:
                    tag += "_"
                used.add(tag)
                typ = self.java(candidate.type) + "T"
                variants += [f"        record {tag}({typ} value) implements {sum_type} {{}}"]
                expression = self.unpack(candidate.type, f"p_{port.name}.bytes()")
                branches += [f'    case "{candidate.schema}" -> new Card.{sum_type}.{tag}({expression});']
            variants += ["    }", ""]
            reads += [
                f"var p_{port.name} = ports.take({name});",
                f"values.put({name}, switch (p_{port.name}.schema()) {{",
                *branches,
                f'    default -> throw new Failure(FailureKind.INPUT_INVALID, "Unknown schema for " + {name});',
                "});",
            ]

        accessors = "\n".join(
            f"        public {self.port_type(port)} {member}() {{\n"
            f"            return get({json.dumps(port.name)}, {self.port_type(port)}.class);\n"
            "        }\n"
            for port, member in inputs
        )
        output_fields = ",\n".join(f"            {self.port_type(port, output=True)} {member}"
                                     for port, member in outputs)
        params = ", ".join(f"{java_type(param.type)} {member}" for param, member in param_fields(card))
        out.mkdir(parents=True, exist_ok=True)
        (out / "Card.java").write_text(f"""// Generated by the Tsugumi Java SDK. Do not edit.
package tsugumi;

import java.util.Map;
import java.util.Optional;

public final class Card {{
    private Card() {{}}

    public record Params({params}) {{}}
    public static Params params() {{ return Parameters.VALUE; }}

{chr(10).join(variants)}
    /** The connected Card inputs, validated before run. */
    public static final class Input {{
        private final Map<String, Object> values;

        Input(Map<String, Object> values) {{
            this.values = values;
        }}

        public <T> T get(String name, Class<T> type) {{
            return find(name, type).orElseThrow(
                    () -> new Failure(FailureKind.INPUT_MISSING, "Missing input " + name));
        }}

        public <T> Optional<T> find(String name, Class<T> type) {{
            Object value = values.get(name);
            if (value != null && !type.isInstance(value)) {{
                throw new Failure(FailureKind.INPUT_INVALID, "Wrong Java type for input " + name);
            }}
            return Optional.ofNullable(type.cast(value));
        }}

{accessors}    }}

    /** Pass null to omit an optional output. */
    public record Output(
{output_fields}) {{}}
}}
""", encoding="utf-8")

        writes = []
        for port, member in outputs:
            name = json.dumps(port.name)
            value = f"value.{member}()"
            table = self.java(port.type, output=True)
            writes += [f"if ({value} != null) {{",
                       f"    var port = {value};",
                       f"    result.add({name}, Wire.pack({name}, builder -> {{",
                       "        UnionChecks.check(port);",
                       f"        return {table}.pack(builder, port);",
                       "    }));",
                       "}"]
        read_code = "\n            ".join(reads)
        write_code = "\n            ".join(writes)
        (out / "Entry.java").write_text(f"""// Generated by the Tsugumi Java SDK.
package tsugumi;

import java.util.HashMap;

final class Entry {{
    static byte[] invoke(Wire.Inputs ports) {{
        var thread = Thread.currentThread();
        var previous = thread.getContextClassLoader();
        try {{
            thread.setContextClassLoader(Entry.class.getClassLoader());
            var values = new HashMap<String, Object>();
            {read_code}
            Card.Output value = CardEntry.run(new Card.Input(values));
            if (value == null) {{
                throw new Failure(FailureKind.OUTPUT_INVALID, "run returned null");
            }}
            var result = new Wire.Outputs();
            {write_code}
            return result.finish();
        }} catch (Throwable error) {{
            return Wire.failure(error, CardEntry.SOURCES);
        }} finally {{
            thread.setContextClassLoader(previous);
        }}
    }}
}}
""", encoding="utf-8")

        roots = dict.fromkeys(self.java(port.type, output=True) for port, _ in outputs)
        checks = union_checks(output_schema, roots)
        (out / "UnionChecks.java").write_text(f"""// Generated by the Tsugumi Java SDK.
package tsugumi;

final class UnionChecks {{
    private UnionChecks() {{}}

{checks}
}}
""", encoding="utf-8")
        return inputs, outputs


def generate_failure_kinds(out, enum_schema):
    enum_body = re.search(r"enum FailureKind\s*:\s*ushort\s*\{(.*?)\}", enum_schema, re.S)[1]
    kinds = re.findall(r"^\s*(\w+)\s*=\s*\d+", enum_body, re.M)
    constants = ",\n".join(
        f"    {kind.upper()}(tsugumi.wire.tsugumi.Data.FailureKind.{kind})"
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



def generate_native(card, names, cpp, output_schema, out):
    input_rows = []
    for port in card.inputs:
        for candidate in port.candidates:
            table = cpp.cpp_type(candidate.type, names)
            input_rows.append(f"    {{{json.dumps(port.name)}, {json.dumps(candidate.schema)}, VerifyJavaTable<{table}>}},")
    roots = [cpp.cpp_type(port.type, names) for port in card.outputs]
    output_rows = [
        f"    {{{json.dumps(port.name)}, {str(port.required).lower()}, ValidJavaOutput<{typ}, ValidOutput>}},"
        for port, typ in zip(card.outputs, roots)
    ]
    visitors = "\n".join(cpp.validators(output_schema, roots))
    (out / "card_adapter.cc").write_text(f"""// Generated by the Tsugumi Java SDK.
#include <array>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "host.hh"

namespace {{
using namespace tsugumi::abi;

{visitors}

constexpr std::array<JavaInput, {len(input_rows)}> inputs{{{{
{chr(10).join(input_rows)}
}}}};
constexpr std::array<JavaOutput, {len(output_rows)}> outputs{{{{
{chr(10).join(output_rows)}
}}}};

auto Run(const tsugumi_call& call) -> ResultBuffer {{
  return RunJava(call, inputs, outputs);
}}
}}  // namespace

extern "C" __attribute__((visibility("default")))
auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {{
  return tsugumi::abi::Invoke(call, result, Run);
}}
""", encoding="utf-8")


def main(cpp_dir, schemas, out, reflection, enums):
    sys.dont_write_bytecode = True
    sys.path.insert(0, cpp_dir)
    import card_cpp

    schemas, out = (Path(path).resolve() for path in (schemas, out))
    card = load_card_schemas(schemas, reflection)
    names = java_schemas(schemas, out / "schemas", card_cpp)
    work = out / "schemas"
    output_schema = reflect_schema(work / "_outputs.fbs", reflection)
    declarations = output_schema["objects"] + output_schema["enums"]
    # The unnamed Java package is inaccessible from the Card adapter.
    output_package = "tsugumi.output." if any("." not in obj["name"] for obj in declarations) else ""
    shared = {"tsugumi.Artifact", "tsugumi.ArtifactId"}
    for obj in declarations:
        if obj["name"] not in shared:
            obj["name"] = output_package + obj["name"]
    java = out / "java"
    for schema in ("tsugumi/card.fbs", "_inputs.fbs", "_outputs.fbs"):
        prefix = ["--java-package-prefix", output_package[:-1]] if output_package and schema == "_outputs.fbs" else []
        subprocess.run(["flatc", "--java", "--gen-object-api", "--gen-all", *prefix,
                        "-I", str(work), "-o", str(java), schema], cwd=work, check=True)
    if output_package:
        for source in (java / "tsugumi/output").rglob("*.java"):
            name = source.stem.removesuffix("T")
            if source.parent == java / "tsugumi/output/tsugumi" and "tsugumi." + name in shared:
                source.unlink()
            else:
                text = source.read_text(encoding="utf-8")
                for type_ in shared:
                    text = text.replace(output_package + type_, type_)
                source.write_text(text, encoding="utf-8")
    artifact = java / "tsugumi/ArtifactT.java"
    text = artifact.read_text()
    artifact.write_text(text.rstrip().removesuffix("}") + """
  public java.nio.file.Path open() { return Artifacts.open(this); }
}
""")
    generator = Generator(card, names, output_package)
    inputs, outputs = generator.generate(java / "tsugumi", Path(enums).read_text(), output_schema)
    generate_failure_kinds(java / "tsugumi", Path(enums).read_text())
    embed_params(card, None, java / "tsugumi/Parameters.java")
    cpp_names, native_schema = card_cpp.generate_headers(schemas, out, reflection)
    generate_native(card, cpp_names, card_cpp, native_schema, out)

    write_api_readme(
        out / "README.md",
        ((port.name, f"input.{member}(): {generator.port_type(port)}") for port, member in inputs),
        ((port.name, f"Output.{member}: {generator.port_type(port, output=True)}"
          + (" (required)" if port.required else "")) for port, member in outputs),
        ((param.name, java_type(param.type)) for param in card.params),
    )


if __name__ == "__main__":
    if sys.argv[1] == "embed":
        _, _, schemas, reflection, values, output = sys.argv
        card = load_card_schemas(Path(schemas), Path(reflection))
        embed_params(card, json.loads(Path(values).read_text()), Path(output))
    else:
        main(*sys.argv[1:])
