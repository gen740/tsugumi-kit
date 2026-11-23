"""Generate the C# Card API from the Card's FlatBuffers schemas."""

import json
import re
import subprocess
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, param_members, reflect_schema, write_api_readme

SCHEMAS = ("tsugumi/card", "_inputs", "_outputs")
# Members every generated class already has.
OBJECT_MEMBERS = {"Equals", "GetHashCode", "GetType", "MemberwiseClone", "ToString"}


def csharp_type(name):
    return "global::" + ".".join("@" + part for part in name.split("."))


def ports(entries, reserved):
    used = set(reserved) | OBJECT_MEMBERS
    for port in entries:
        prop = port.name[:1].upper() + port.name[1:]
        while prop in used:
            prop += "_"
        used.add(prop)
        yield port, "@" + prop


def fix_pack_collisions(sources):
    """Keep flatc's Pack method unless a generated type or member already uses that name."""
    renamed = {}
    for path, text in sources.items():
        method = re.search(r"^  public static Offset<([\w.]+)> (Pack)\(", text, re.M)
        if not method:
            continue
        type_name = method[1]
        short_name = type_name.rsplit(".", 1)[-1]
        # Before the static Pack method, flatc emits the raw type's accessors.
        members = set(re.findall(r"^  public (?:static )?\S+ (\w+)[ (]", text[:method.start()], re.M))
        if short_name != "Pack" and "Pack" not in members:
            continue
        pack_name = "Pack_"
        while pack_name in members:
            pack_name += "_"
        renamed[type_name] = pack_name
        text = text[:method.start(2)] + pack_name + text[method.end(2):]
        # SerializeToBinary calls the method without its namespace in this file.
        text = re.sub(rf"(?<![\w.]){re.escape(short_name)}\.Pack\(fbb, this\)",
                      f"{short_name}.{pack_name}(fbb, this)", text)
        sources[path] = text

    if renamed:
        names = "|".join(re.escape(name) for name in renamed)
        calls = re.compile(rf"(?<![\w.])({names})\.Pack\(")
        for path, text in sources.items():
            sources[path] = calls.sub(lambda match: f"{match[1]}.{renamed[match[1]]}(", text)
    return {csharp_type(name): method for name, method in renamed.items()}


def generate_bindings(schemas, out):
    """Run flatc and make its output compile for any port namespace."""
    for schema in SCHEMAS:
        subprocess.run(["flatc", "--csharp", "--gen-all", "--gen-object-api", "-I", str(schemas), "-o", str(out),
                        str(schemas / f"{schema}.fbs")], check=True)
    sources = {source: source.read_text(encoding="utf-8") for source in out.rglob("*.cs")}
    pack_methods = fix_pack_collisions(sources)
    for generated, text in sources.items():
        # flatc emits namespace references both qualified and unqualified. The
        # segments of generated port namespaces may be C# keywords (e.g. "out").
        text = re.sub(
            r'("(?:[^"\\]|\\.)*"|//[^\n]*)|\b(inputs|outputs)(?:\.[A-Za-z_]\w*)*',
            lambda match: match[0] if match[1] else ".".join("@" + part for part in match[0].split(".")),
            text,
        )
        # flatc's verifier accepts unknown union discriminators by default.
        # Preserve NONE while rejecting unknown tags at the input/output boundary.
        text = re.sub(
            r"(switch\(\(([A-Za-z_]\w*)\)typeId\)[\s\S]*?)default: result = true;",
            r"\1case \2.NONE: result = true; break;\n      default: result = false;",
            text,
        )
        generated.write_text(text, encoding="utf-8")
    artifact = out / "tsugumi/Artifact.cs"
    text = artifact.read_text()
    text = text.replace("public class ArtifactT", "public partial class ArtifactT")
    artifact.write_text(text, encoding="utf-8")
    return pack_methods


def cs_type(type_):
    if type_.startswith("["):
        return "IReadOnlyList<" + cs_type(type_[1:-1]) + ">"
    return {"bool": "bool", "long": "long", "double": "double", "string": "string"}[type_]


def cs_literal(value, type_):
    if type_.startswith("["):
        members = ", ".join(cs_literal(item, type_[1:-1]) for item in value)
        return f"Array.AsReadOnly(new {cs_type(type_[1:-1])}[] {{ {members} }})"
    if type_ == "long":
        return str(value) + "L"
    if type_ == "double":
        return repr(float(value)) + "D"
    return json.dumps(value)


def param_fields(card):
    used = {"Params", "Clone", "EqualityContract", "PrintMembers", "Deconstruct"} | OBJECT_MEMBERS
    names = list(param_members((param.name for param in card.params), ()))
    result = []
    for param, name in zip(card.params, names):
        prop = name[:1].upper() + name[1:]
        while prop in used:
            prop += "_"
        used.add(prop)
        result.append((param, "@" + prop))
    return result


def embed_params(card, values, output):
    arguments = []
    for param, _ in param_fields(card):
        if values is None:
            value = [] if param.type.startswith("[") else {
                "bool": False, "long": 0, "double": 0.0, "string": "",
            }[param.type]
        else:
            value = values[param.name]
        arguments.append(cs_literal(value, param.type))
    output.write_text(f"""// Generated by the Tsugumi C# SDK.
using System;
namespace Tsugumi;
internal static class EmbeddedParams
{{
    internal static readonly Params Value = new({", ".join(arguments)});
}}
""", encoding="utf-8")


def artifact_visitors(schema, roots):
    objects, enums = schema["objects"], schema["enums"]
    by_name = {obj["name"]: index for index, obj in enumerate(objects)}
    functions = {}

    def table(index):
        if (False, index) in functions:
            return
        obj = objects[index]
        typ = csharp_type(obj["name"]) + "T"
        functions[False, index] = body = [
            f"    internal static void Collect({typ}? value, List<global::tsugumi.ArtifactIdT> ids)",
            "    {",
            "        if (value is null) return;",
        ]
        if obj["name"] == "tsugumi.Artifact":
            body += [
                "        if (value.Id?.Bytes is not { Length: 16 } id)",
                '            throw InputPorts.Rejected(FailureKind.output_invalid, "Invalid Artifact ID");',
                "        foreach (var item in ids)",
                "            if (id.AsSpan().SequenceEqual(item.Bytes)) return;",
                "        ids.Add(new() { Bytes = (byte[])id.Clone() });",
            ]
        else:
            for field in obj["fields"]:
                kind = field["type"]
                base = kind["base_type"]
                vector = base in {"Vector", "Vector64"}
                element = kind.get("element") if vector else base
                child = kind.get("index", -1)
                if field.get("deprecated") or element not in {"Obj", "Union"}:
                    continue
                if element == "Obj" and objects[child].get("is_struct"):
                    continue
                prop = re.sub(r"_(.)", lambda m: m[1].upper(), "_" + field["name"])
                value = f"value.@{prop}"
                if element == "Union":
                    union(child)
                    required = str(vector or field.get("required", False)).lower()
                    args = f", {required}"
                else:
                    table(child)
                    args = ""
                if vector:
                    body += [f"        if ({value} is {{ }} list{prop})",
                             f"            foreach (var child in list{prop}) Collect(child, ids{args});"]
                else:
                    body += [f"        Collect({value}, ids{args});"]
        body += ["    }", ""]

    def union(index):
        if (True, index) in functions:
            return
        enum = enums[index]
        typ = csharp_type(enum["name"])
        functions[True, index] = body = [
            f"    internal static void Collect({typ}Union? choice, List<global::tsugumi.ArtifactIdT> ids, bool required)",
            "    {",
            f"        switch (choice?.Type ?? {typ}.NONE)",
            "        {",
            f"            case {typ}.NONE when !required && choice?.Value is null: return;",
        ]
        for variant in enum["values"][1:]:
            child = variant["union_type"]
            if child.get("base_type") == "Obj":
                child_obj = objects[child["index"]]
                variant_type = csharp_type(child_obj["name"]) + "T"
            else:
                child_obj = None
                variant_type = "string"
            tag = variant["name"]
            body.append(f"            case {typ}.@{tag} when choice?.Value is {variant_type} item{tag}:")
            if child_obj and not child_obj.get("is_struct"):
                table(child["index"])
                body.append(f"                Collect(item{tag}, ids);")
            body.append("                return;")
        body += [
            "            default:",
            '                throw InputPorts.Rejected(FailureKind.output_invalid, "Invalid output union");',
            "        }", "    }", "",
        ]

    for root in roots:
        table(by_name[root])
    return [line for body in functions.values() for line in body]


class Generator:
    def __init__(self, card, pack_methods):
        self.card = card
        self.pack_methods = pack_methods

    def pack(self, type_):
        name = csharp_type(type_)
        return f"{name}.{self.pack_methods.get(name, 'Pack')}"

    def port_type(self, port):
        if len(port.candidates) > 1:
            return f"Input_{port.name}"
        return csharp_type(port.type) + "T"

    def unpack(self, type_, variable, name):
        table = csharp_type(type_)
        root = type_.rsplit(".", 1)[-1]
        return (f"{table}.GetRootAs{root}(InputPorts.Verify({variable}.GetBytesArray(), "
                f"{table}Verify.Verify, {name}, FailureKind.input_invalid)).UnPack()")

    def generate(self, schema):
        card = self.card
        inputs = list(ports(card.inputs, {"Input", "Get", "Find"}))
        outputs = list(ports(card.outputs, {"Output", "ToWire"}))
        variants = []
        reads = []
        for index, (port, prop) in enumerate(inputs):
            name = json.dumps(port.name)
            variable = f"port{index}"
            reads.append(f"        var {variable} = ports.Take({name});")
            if len(port.candidates) == 1:
                candidate, = port.candidates
                reads.append(f'        if ({variable}.Schema != "{candidate.schema}") throw InputPorts.UnknownSchema({name});')
                reads.append(f"        {prop} = {self.unpack(candidate.type, variable, name)};")
            else:
                sum_type = self.port_type(port)
                variants += [f"public abstract record {sum_type}", "{", f"    private {sum_type}() {{}}"]
                reads += [f"        {prop} = {variable}.Schema switch", "        {"]
                used = set(OBJECT_MEMBERS) | {"Clone", "EqualityContract", "PrintMembers", "Deconstruct", sum_type}
                for candidate in port.candidates:
                    leaf = candidate.type.rsplit(".", 1)[-1]
                    while leaf in used:
                        leaf += "_"
                    used.add(leaf)
                    typ = csharp_type(candidate.type)
                    variants += [f"    public sealed record @{leaf}({typ}T Value) : {sum_type};"]
                    reads += [f'            "{candidate.schema}" => new {sum_type}.@{leaf}({self.unpack(candidate.type, variable, name)}),']
                variants += ["}", ""]
                reads += [f"            _ => throw InputPorts.UnknownSchema({name}),", "        };"]
            reads.append(f"        values.Add({name}, {prop});")

        input_lines = [
            "public sealed class Input",
            "{",
            "    private readonly Dictionary<string, object> values = new(StringComparer.Ordinal);",
            "",
            *(f"    public {self.port_type(port)} {prop} {{ get; }}" for port, prop in inputs),
            "",
            "    internal Input(InputPorts ports)",
            "    {", *reads, "    }", "",
            "    public T Get<T>(string name) where T : class =>",
            '        Find<T>(name) ?? throw InputPorts.Rejected(FailureKind.input_missing, "Missing input " + name);',
            "",
            "    public T? Find<T>(string name) where T : class",
            "    {",
            "        if (!values.TryGetValue(name, out var value)) return null;",
            '        return value as T ?? throw InputPorts.Rejected(FailureKind.input_invalid, "Wrong C# type for input " + name);',
            "    }", "}",
        ]

        output_lines = ["public sealed class Output", "{",
                        *(f"    public {self.port_type(port)}? {prop} {{ get; set; }}" for port, prop in outputs),
                        "", "    internal Wire.CardResultT ToWire()",
                        "    {",
                        "        var result = new Wire.CardResultT { Outputs = new List<Wire.CardPortT>() };"]
        for index, (port, prop) in enumerate(outputs):
            name = json.dumps(port.name)
            if port.required:
                output_lines += [
                    f"        if ({prop} is null)",
                    f'            throw InputPorts.Rejected(FailureKind.output_invalid, "Missing output " + {name});',
                ]
            table = csharp_type(port.type)
            output_lines += [
                f"        if ({prop} is {{ }} value{index})",
                "        {",
                "            var ids = new List<global::tsugumi.ArtifactIdT>();",
                f"            Ports.Collect(value{index}, ids);",
                f"            var bytes = InputPorts.Pack({name}, {table}Verify.Verify, "
                f"builder => {self.pack(port.type)}(builder, value{index}).Value);",
                f"            result.Outputs.Add(InputPorts.FlatBufferPort({name}, bytes, ids));",
                "        }",
            ]
        output_lines += ["        return result;", "    }", "}"]

        params = ", ".join(f"{cs_type(param.type)} {prop}" for param, prop in param_fields(card))
        params_declaration = f"public sealed record Params({params})" if params else "public sealed record Params"
        source = [
            "// Generated by the Tsugumi C# SDK. Do not edit.",
            "#nullable enable",
            "using System;",
            "using System.Collections.Generic;",
            "using Google.FlatBuffers;",
            "using Tsugumi.Data;",
            "using Wire = Tsugumi.Card;",
            "namespace Tsugumi;",
            "",
            params_declaration,
            "{",
            "    public static Params Current => EmbeddedParams.Value;",
            "}",
            "",
            *variants,
            *input_lines,
            "",
            *output_lines,
            "",
            "internal static class Ports",
            "{",
            *artifact_visitors(schema, dict.fromkeys(port.type for port, _ in outputs)),
            "}",
            "",
        ]
        return "\n".join(source), inputs, outputs


def readable(type_):
    return type_.replace("global::", "").replace("@", "")


def main(schemas, reflection, out):
    schemas, reflection, out = (Path(path).resolve() for path in (schemas, reflection, out))
    out.mkdir(parents=True, exist_ok=True)
    pack_methods = generate_bindings(schemas, out / "Ports")
    card = load_card_schemas(schemas, reflection)
    generator = Generator(card, pack_methods)
    schema = reflect_schema(schemas / "_outputs.fbs", reflection)
    source, inputs, outputs = generator.generate(schema)
    (out / "Card.g.cs").write_text(source, encoding="utf-8")
    embed_params(card, None, out / "EmbeddedParams.g.cs")

    write_api_readme(
        out / "README.md",
        ((port.name, f"Input.{prop[1:]}: {readable(generator.port_type(port))}") for port, prop in inputs),
        ((port.name, f"Output.{prop[1:]}: {readable(generator.port_type(port))}?"
          + (" (required)" if port.required else "")) for port, prop in outputs),
        ((param.name, cs_type(param.type)) for param in card.params),
    )


if __name__ == "__main__":
    if sys.argv[1] == "embed":
        _, _, schemas, reflection, values, output = sys.argv
        card = load_card_schemas(Path(schemas), Path(reflection))
        embed_params(card, json.loads(Path(values).read_text()), Path(output))
    else:
        main(*sys.argv[1:])
