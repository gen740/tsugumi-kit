"""Generate the C# Card API from the Card's FlatBuffers schemas."""

import json
import re
import subprocess
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, write_api_readme

SCHEMAS = ("_inputs", "_outputs", "_params")
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
        subprocess.run(["flatc", "--csharp", "--gen-object-api", "-o", str(out),
                        str(schemas / f"{schema}.fbs")], check=True)
    sources = {source: source.read_text(encoding="utf-8") for source in out.rglob("*.cs")}
    pack_methods = fix_pack_collisions(sources)
    for generated, text in sources.items():
        # flatc emits namespace references both qualified and unqualified. The
        # segments of generated port namespaces may be C# keywords (e.g. "out").
        text = re.sub(
            r'("(?:[^"\\]|\\.)*"|//[^\n]*)|\b(inputs|outputs|parameters)(?:\.[A-Za-z_]\w*)*',
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
    return pack_methods


class Generator:
    def __init__(self, card, pack_methods):
        self.card = card
        self.pack_methods = pack_methods
        self.helpers = []

    def pack(self, type_):
        name = csharp_type(type_)
        return f"{name}.{self.pack_methods.get(name, 'Pack')}"

    def decode_merge(self, type_, members):
        function = f"DecodeMerge{len(self.helpers)}"
        self.helpers.append(lines := [
            f"    internal static object {function}({csharp_type(type_)}T merged, string name)",
            "    {",
            "        switch (merged.Value?.Value)",
            "        {",
        ])
        for member in members:
            table = csharp_type(member.type)
            root = member.type.rsplit(".", 1)[-1]
            value = (f"{table}.GetRootAs{root}(InputPorts.Verify(box.Bytes?.ToArray(), "
                     f"{table}Verify.Verify, name, FailureKind.invalid_request)).UnPack()")
            if member.merge:
                value = f"{self.decode_merge(member.type, member.merge)}({value}, name)"
            lines += [f"            case {csharp_type(member.box_type)}T box:",
                      f"                return {value};"]
        lines += ["            default:",
                  '                throw InputPorts.Rejected(FailureKind.invalid_request, "Invalid Merge input " + name);',
                  "        }", "    }", ""]
        return function

    def encode_merge(self, type_, members):
        function = f"EncodeMerge{len(self.helpers)}"
        union = csharp_type(members[0].union_type)
        self.helpers.append(lines := [
            f"    internal static {csharp_type(type_)}T? {function}(object value, string name)",
            "    {",
        ])
        for index, member in enumerate(members):
            table = csharp_type(member.type)
            box = csharp_type(member.box_type)
            if member.merge:
                nested = self.encode_merge(member.type, member.merge)
                lines.append(f"        if ({nested}(value, name) is {{ }} item{index})")
            else:
                lines.append(f"        if (value is {table}T item{index})")
            pack = (f"InputPorts.Pack(name, {table}Verify.Verify, "
                    f"builder => {self.pack(member.type)}(builder, item{index}).Value)")
            lines += [
                "            return new() { Value = new() {",
                f"                Type = {union}.@{member.tag},",
                f"                Value = new {box}T {{ Bytes = new List<byte>({pack}) }},",
                "            } };",
            ]
        lines += ["        return null;", "    }", ""]
        return function

    def port_type(self, port):
        if port.kind == "Path":
            return "string"
        return "object" if port.merge else csharp_type(port.type) + "T"

    def generate(self):
        card = self.card
        inputs = list(ports(card.inputs, {"Input", "Params", "Get", "Find"}))
        outputs = list(ports(card.outputs, {"Output", "ToWire"}))
        params = csharp_type(card.params_type)
        input_lines = [
            "public sealed class Input",
            "{",
            "    private readonly Dictionary<string, object> values = new(StringComparer.Ordinal);",
            "",
            f"    public {params}T Params {{ get; }}",
        ]
        for port, prop in inputs:
            input_lines.append(f"    public {self.port_type(port)}? {prop} {{ get; }}")
        input_lines += [
            "",
            "    internal Input(InputPorts ports)",
            "    {",
            f'        Params = {params}.GetRootAs{card.params_type.rsplit(".", 1)[-1]}('
            f'InputPorts.Verify(ports.Params, {params}Verify.Verify, "params", FailureKind.invalid_request)).UnPack();',
        ]
        for index, (port, prop) in enumerate(inputs):
            name = json.dumps(port.name)
            input_lines.append(f"        if (ports.Take({name}, PortKind.{port.kind}) is {{ }} port{index})")
            input_lines.append("        {")
            if port.kind == "Path":
                value = f"InputPorts.PathOf(port{index})"
            else:
                table = csharp_type(port.type)
                root = port.type.rsplit(".", 1)[-1]
                value = (f"{table}.GetRootAs{root}(InputPorts.Verify(port{index}.GetBytesArray(), "
                         f"{table}Verify.Verify, {name}, FailureKind.invalid_request)).UnPack()")
                if port.merge:
                    value = f"Ports.{self.decode_merge(port.type, port.merge)}({value}, {name})"
            input_lines += [f"            {prop} = {value};",
                            f"            values.Add({name}, {prop});",
                            "        }"]
        input_lines += [
            "    }",
            "",
            "    /// <summary>A needed input. Throws input_missing when the port is absent.</summary>",
            "    public T Get<T>(string name) where T : class =>",
            '        Find<T>(name) ?? throw InputPorts.Rejected(FailureKind.input_missing, "Missing input " + name);',
            "",
            "    /// <summary>An input that may be absent, including a port that is not wired.</summary>",
            "    public T? Find<T>(string name) where T : class",
            "    {",
            "        if (!values.TryGetValue(name, out var value))",
            "            return null;",
            "        return value as T ?? throw InputPorts.Rejected(FailureKind.invalid_request, \"Wrong C# type for input \" + name);",
            "    }",
            "}",
        ]

        output_lines = ["public sealed class Output", "{"]
        for port, prop in outputs:
            modifier = "required " if port.required else ""
            suffix = "" if port.required else "?"
            output_lines.append(f"    public {modifier}{self.port_type(port)}{suffix} {prop} {{ get; set; }}")
        output_lines += [
            "",
            "    internal Wire.CardResultT ToWire()",
            "    {",
            "        var result = new Wire.CardResultT { Outputs = new List<Wire.CardPortT>() };",
        ]
        for index, (port, prop) in enumerate(outputs):
            name = json.dumps(port.name)
            if port.required:
                output_lines += [
                    f"        if ({prop} is null)",
                    f'            throw InputPorts.Rejected(FailureKind.output_invalid, "Missing output " + {name});',
                ]
            output_lines.append(f"        if ({prop} is {{ }} value{index})")
            if port.kind == "Path":
                output_lines.append(f"            result.Outputs.Add(InputPorts.PathPort({name}, value{index}));")
                continue
            table = csharp_type(port.type)
            item = f"value{index}"
            if port.merge:
                encode = self.encode_merge(port.type, port.merge)
                item = (f"(Ports.{encode}(value{index}, {name}) ?? throw InputPorts.Rejected("
                        f'FailureKind.output_invalid, "Invalid Merge output " + {name}))')
            output_lines.append(
                f"            result.Outputs.Add(InputPorts.FlatBufferPort({name}, InputPorts.Pack({name}, "
                f"{table}Verify.Verify, builder => {self.pack(port.type)}(builder, {item}).Value)));")
        output_lines += ["        return result;", "    }", "}"]

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
            *input_lines,
            "",
            *output_lines,
            "",
            "internal static class Ports",
            "{",
            *(line for helper in self.helpers for line in helper),
            "}",
            "",
        ]
        return "\n".join(source), inputs, outputs


def readable(type_):
    return type_.replace("global::", "").replace("@", "")


def merge_types(members):
    for member in members:
        if member.merge:
            yield from merge_types(member.merge)
        else:
            yield member.type


def main(schemas, reflection, out):
    schemas, reflection, out = (Path(path).resolve() for path in (schemas, reflection, out))
    out.mkdir(parents=True, exist_ok=True)
    pack_methods = generate_bindings(schemas, out / "Ports")
    card = load_card_schemas(schemas, reflection)
    generator = Generator(card, pack_methods)
    source, inputs, outputs = generator.generate()
    (out / "Card.g.cs").write_text(source, encoding="utf-8")

    def described(port):
        if port.merge:
            members = " | ".join(type_ + "T" for type_ in merge_types(port.merge))
            return f"object ({members})"
        return readable(generator.port_type(port))

    write_api_readme(
        out / "README.md",
        ((port.name, f"Input.{prop[1:]}: {described(port)}?") for port, prop in inputs),
        ((port.name, f"Output.{prop[1:]}: {described(port)}"
          + (" (required)" if port.required else "?")) for port, prop in outputs),
        ((param.name, param.type) for param in card.params),
    )


if __name__ == "__main__":
    main(*sys.argv[1:])
