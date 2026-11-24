"""Generate the Ruby SDK's C++ glue and the Card API from the Card's schemas.

Usage: card_ruby.py <cpp sdk dir> <schemas> <out> <reflection.fbs>

Writes out/include (flatc C++ headers), out/src/card_glue.cc and out/api
(card.rbs and README.md). The C++ SDK's header generator supplies the
object API the glue converts to and from the JSON passed to Ruby.
"""

import json
import re
import sys
from pathlib import Path

if sys.argv[1] == "--params":
    values = json.loads(Path(sys.argv[2]).read_text())

    def ruby_value(value):
        if isinstance(value, list):
            return "[" + ", ".join(map(ruby_value, value)) + "].freeze"
        literal = json.dumps(value, ensure_ascii=False).replace("#", r"\#")
        return f"{literal}.freeze" if isinstance(value, str) else literal

    pairs = (f"{ruby_value(key)} => {ruby_value(value)}" for key, value in values.items())
    Path(sys.argv[3]).write_text(
        "module Tsugumi\n  PARAMS = {" + ", ".join(pairs) + "}.freeze\nend\n"
    )
    sys.exit(0)

sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])

from card_cpp import FLATC_KEYWORDS, cpp_type, flatc_identifier, generate_headers, validators  # noqa: E402
from tsugumi_ports import fields, load_card_schemas, reflect_schema, write_api_readme  # noqa: E402

SCHEMAS = ("_inputs", "_outputs")
SCALARS = {
    "Bool": "bool",
    "Byte": "int8_t",
    "UByte": "uint8_t",
    "Short": "int16_t",
    "UShort": "uint16_t",
    "Int": "int32_t",
    "UInt": "uint32_t",
    "Long": "int64_t",
    "ULong": "uint64_t",
    "Float": "float",
    "Double": "double",
    "String": "std::string",
}
RBS_SCALARS = {
    "Bool": "bool",
    "Float": "float",
    "Double": "float",
    "String": "String",
    **{kind: "Integer" for kind in SCALARS if kind not in {"Bool", "Float", "Double", "String"}},
}


def quote(value):
    return json.dumps(value, ensure_ascii=False)


def base(spec):
    return spec.get("base_type", "None")


def table_fields(obj):
    return [
        field for field in fields(obj)
        if not field.get("deprecated", False)
        and field["type"].get("element", "None") != "UType"
    ]


def merge_names(members):
    """Ruby names of Merge members: the short type name, qualified on a clash."""
    short = [member.type.rsplit(".", 1)[-1] for member in members]
    return [name if short.count(name) == 1 else member.type
            for name, member in zip(short, members)]


class Glue:
    """C++ conversions between the flatc object API and Ruby's JSON values."""

    def __init__(self, names):
        self.names = names

    def cxx(self, name):
        return cpp_type(name, self.names)

    def member(self, name):
        # flatc appends an underscore to the keywords card_cpp leaves in place.
        return name + "_" if name in FLATC_KEYWORDS else self.names.get(name, name)

    def variant(self, name):
        return flatc_identifier(self.names.get(name, name))

    def object_type(self, obj):
        return self.cxx(obj["name"]) + ("" if obj.get("is_struct", False) else "T")

    def field_type(self, kind, index, schema):
        if kind == "Obj":
            return self.object_type(schema["objects"][index])
        if index >= 0:
            return self.cxx(schema["enums"][index]["name"]) + ("Union" if kind == "Union" else "")
        return SCALARS[kind]

    @staticmethod
    def codec(kind, encode, decode):
        return [
            f"auto Codec<{kind}>::Encode(const {kind}& value) -> Json {{",
            *("  " + line for line in encode),
            "}",
            f"auto Codec<{kind}>::Decode(const Json& value) -> {kind} {{",
            *("  " + line for line in decode),
            "}",
            "",
        ]

    def emit_object(self, obj, schema):
        fields = table_fields(obj)
        if obj["name"] == "tsugumi.Artifact":
            return self.codec("::tsugumi::ArtifactT", [
                'return Json{{"$artifact", std::vector<uint8_t>(value.id->bytes()->begin(), value.id->bytes()->end())}};',
            ], [
                'const auto bytes = value.at("$artifact").get<std::array<uint8_t, 16>>();',
                '::tsugumi::ArtifactT result;',
                'result.id = std::make_unique<::tsugumi::ArtifactId>(flatbuffers::span<const uint8_t, 16>(bytes));',
                'return result;',
            ])
        struct = obj.get("is_struct", False)
        kind = self.object_type(obj)
        encode = ["return Json{"] if fields else ["(void)value;", "return Json::object();"]
        decode = [f"CheckObject(value, {{{', '.join(quote(f['name']) for f in fields)}}});"]
        if not struct:
            decode.append(f"{kind} result;")
        arguments = []
        for field in fields:
            spec = field["type"]
            name = quote(field["name"])
            accessor = self.member(field["name"])
            method = "ruby::Encode"
            member = f"value.{accessor}"
            if struct:
                member += "()"
                if base(spec) == "Array":
                    elem = self.field_type(spec.get("element", "None"), spec.get("index", -1), schema)
                    length = spec["fixed_length"]
                    # flatc stores fixed bool arrays as bytes in the C++ API.
                    storage = "uint8_t" if spec.get("element") == "Bool" else elem
                    method = f"EncodeArray<{elem}>"
                    member = "*" + member
                    decode.append(f"const auto array_{accessor} = "
                                  f"DecodeArray<{elem}, {length}, {storage}>(value.at({name}));")
                    arguments.append(f"flatbuffers::span<const {storage}, {length}>"
                                     f"(array_{accessor}.data(), {length})")
                else:
                    kind_ = self.field_type(base(spec), spec.get("index", -1), schema)
                    arguments.append(f"Codec<{kind_}>::Decode(value.at({name}))")
            else:
                required = str(field.get("required", False)).lower()
                decode.append(f"ReadMember(value, {name}, result.{accessor}, {required});")
            encode.append(f"  {{{name}, {method}({member})}},")
        if fields:
            encode.append("};")
        decode.append(f"return {kind}({', '.join(arguments)});" if struct else "return result;")
        return self.codec(kind, encode, decode)

    def emit_enum(self, enum):
        kind = self.cxx(enum["name"])
        underlying = SCALARS[base(enum["underlying_type"])]
        values = enum.get("values", [])
        encode = [f"if (value == {kind}::{self.member(item['name'])}) return {quote(item['name'])};"
                  for item in values]
        decode = [f"if (value == {quote(item['name'])}) return {kind}::{self.member(item['name'])};"
                  for item in values]
        encode.append(f"return static_cast<{underlying}>(value);")
        decode.append(f"return static_cast<{kind}>(Codec<{underlying}>::Decode(value));")
        return self.codec(kind, encode, decode)

    def emit_union(self, enum, schema):
        kind = self.cxx(enum["name"])
        native = kind + "Union"
        encode = ["switch (value.type) {"]
        decode = [
            "if (value.is_null()) return {};",
            'CheckObject(value, {"type", "value"});',
            'const auto& tag = value.at("type");',
        ]
        for item in enum.get("values", []):
            spec = item.get("union_type", {})
            tag = self.variant(item["name"])
            if base(spec) == "None":
                encode.append(f"  case {kind}::{tag}: return nullptr;")
                continue
            member = self.field_type(base(spec), spec.get("index", -1), schema)
            name = quote(item["name"])
            encode += [
                f"  case {kind}::{tag}:",
                f"    if (const auto* member = value.As{tag}())",
                f'      return Json{{{{"type", {name}}}, {{"value", ruby::Encode(*member)}}}};',
                "    break;",
            ]
            decode += [
                f"if (tag == {name}) {{",
                f"  {native} result;",
                f'  result.value = new {member}(Codec<{member}>::Decode(value.at("value")));',
                f"  result.type = {kind}::{tag};",
                "  return result;",
                "}",
            ]
        encode += ["}", 'abi::Reject(abi::FailureKind::invalid_request, "invalid union");']
        decode.append('throw std::invalid_argument("invalid union tag");')
        return self.codec(native, encode, decode)



def run_card(card, glue):
    cxx = glue.cxx
    lines = [
        "auto RunCard(tsugumi_bytes bytes) -> CardResultT {",
        "  abi::Input input(bytes);",
        "  Json values = Json::object();",
    ]
    for port in card.inputs:
        name = quote(port.name)
        lines += [f"  if (const auto entry = input.ReadPort({name})) {{"]
        for index, (member, tag) in enumerate(zip(port.candidates, merge_names(port.candidates))):
            value = f"Encode(abi::ReadValue<{cxx(member.type)}>(entry->bytes, {name}))"
            if len(port.candidates) > 1:
                value = f'Json{{{{"type", {quote(tag)}}}, {{"value", {value}}}}}'
            keyword = "if" if index == 0 else "else if"
            lines += [f'    {keyword} (entry->schema == "{member.schema}")',
                      f"      values[{name}] = {value};"]
        lines += [f'    else abi::Reject(FailureKind::input_invalid, "Unknown schema for " + std::string({name}));',
                  '  } else {',
                  f'    abi::Reject(FailureKind::input_missing, "Missing input " + std::string({name}));',
                  '  }']
    lines += [
        "  input.CheckConsumed();",
        '  const auto reply = InvokeRuby(Json{{"ports", std::move(values)}});',
        '  if (reply.contains("failure")) return FailureResult(reply.at("failure"));',
        '  const auto& outputs = reply.at("outputs");',
        "  CardResultT result;",
        "  try {",
        f"    CheckObject(outputs, {{{', '.join(quote(port.name) for port in card.outputs)}}});",
    ]
    for index, port in enumerate(card.outputs):
        name = quote(port.name)
        required = str(port.required).lower()
        table = cxx(port.type)
        native = table + "T"
        decode = f"Codec<{native}>::Decode(*value)"
        lines += [
            f"    std::optional<{native}> output_{index};",
            f"    if (const auto value = outputs.find({name}); value != outputs.end() && !value->is_null())",
            f"      output_{index} = {decode};",
            f"    abi::WritePort<{table}>(result, {name}, output_{index}, {required}, ValidOutput);",
        ]
    return lines + [
        "  } catch (const tsugumi::Failure&) {",
        "    throw;",
        "  } catch (const std::exception& error) {",
        "    abi::Reject(FailureKind::output_invalid, error.what());",
        "  }",
        "  return result;",
        "}",
    ]


def generate_glue(card, schemas, names, output_schema):
    glue = Glue(names)
    bodies = []
    codecs = set()
    for schema in schemas:
        for obj in schema.get("objects", []):
            kind = glue.object_type(obj)
            if kind not in codecs:
                codecs.add(kind)
                bodies += glue.emit_object(obj, schema)
        for enum in schema.get("enums", []):
            union = enum.get("is_union", False)
            kind = glue.cxx(enum["name"]) + ("Union" if union else "")
            if kind not in codecs:
                codecs.add(kind)
                bodies += glue.emit_union(enum, schema) if union else glue.emit_enum(enum)
    run = run_card(card, glue)
    declarations = []
    for kind in sorted(codecs):
        declarations += [
            f"template <> struct Codec<{kind}> {{",
            f"  static auto Encode(const {kind}& value) -> Json;",
            f"  static auto Decode(const Json& value) -> {kind};",
            "};",
        ]
    return "\n".join([
        "// Generated by the Tsugumi Ruby SDK. Do not edit.",
        '#include <filesystem>',
        '#include "adapter.hh"',
        '#include "json_codec.hh"',
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        "",
        "namespace tsugumi::ruby {",
        "",
        "using abi::FailureKind;",
        "using Tsugumi::Card::CardResultT;",
        "auto InvokeRuby(const Json& input) -> Json;",
        "auto FailureResult(const Json& failure) -> CardResultT;",
        "",
        *declarations,
        "",
        *bodies,
        *validators(output_schema, (glue.cxx(port.type) for port in card.outputs)),
        *run,
        "",
        "}  // namespace tsugumi::ruby",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi::Invoke(call, result, tsugumi::ruby::RunCard);",
        "}",
        "",
    ])


class Signatures:
    """RBS types of the Ruby values the SDK passes to and accepts from main."""

    def __init__(self, schemas):
        self.objects = {obj["name"]: (obj, schema) for schema in schemas
                        for obj in schema.get("objects", [])}
        self.enums = {enum["name"]: (enum, schema) for schema in schemas
                      for enum in schema.get("enums", [])}
        self.aliases = {}
        self.alias_names = {}

    def alias(self, name):
        if name not in self.alias_names:
            stem = name.replace(".", "__")
            if not re.match(r"[a-z]", stem):
                stem = "t_" + stem
            alias = stem
            suffix = 2
            while alias in self.aliases or alias in {"float", "output"}:
                alias = f"{stem}_{suffix}"
                suffix += 1
            self.alias_names[name] = alias
        return self.alias_names[name]

    def scalar(self, kind, index, schema):
        if kind == "Obj":
            return self.table(schema["objects"][index]["name"])
        if index >= 0:
            return self.enum(schema["enums"][index]["name"])
        return RBS_SCALARS[kind]

    def field(self, field, schema, *, in_struct=False):
        spec = field["type"]
        kind = base(spec)
        if kind in {"Vector", "Vector64", "Array"}:
            element = spec.get("element", "None")
            if element == "UByte" and kind != "Array":
                return "String"  # Base64, see Tsugumi.decode_bytes.
            item = self.scalar(element, spec.get("index", -1), schema)
            return f"Array[{item}{'?' if element == 'Union' else ''}]"
        value = self.scalar(kind, spec.get("index", -1), schema)
        nullable = not in_struct and kind in {"Obj", "Union"} and not field.get("required", False)
        return value + "?" if nullable or field.get("optional", False) else value

    def table(self, name):
        if name == "tsugumi.Artifact":
            return "Artifact"
        alias = self.alias(name)
        if alias not in self.aliases:
            self.aliases[alias] = None
            obj, schema = self.objects[name]
            in_struct = obj.get("is_struct", False)
            entries = ", ".join(
                f"{quote(field['name'])} => {self.field(field, schema, in_struct=in_struct)}"
                for field in table_fields(obj)
            )
            self.aliases[alias] = "{ " + entries + " }" if entries else "Hash[String, untyped]"
        return alias

    def enum(self, name):
        alias = self.alias(name)
        if alias not in self.aliases:
            self.aliases[alias] = None
            enum, schema = self.enums[name]
            if enum.get("is_union", False):
                members = []
                for item in enum.get("values", []):
                    spec = item.get("union_type", {})
                    if base(spec) != "None":
                        value = self.scalar(base(spec), spec.get("index", -1), schema)
                        members.append(f'{{ "type" => {quote(item["name"])}, "value" => {value} }}')
                self.aliases[alias] = " | ".join(members) or "nil"
            else:
                names = [quote(item["name"]) for item in enum.get("values", [])]
                self.aliases[alias] = " | ".join([*names, "Integer"])
        return alias

    def port(self, port):
        if len(port.candidates) > 1:
            return "(" + " | ".join(
                f'{{ "type" => {quote(tag)}, "value" => {self.table(member.type)} }}'
                for member, tag in zip(port.candidates, merge_names(port.candidates))
            ) + ")"
        return self.table(port.type)


def generate_api(card, schemas, out):
    signatures = Signatures(schemas)
    inputs = [(port.name, signatures.port(port)) for port in card.inputs]
    outputs = [(port, signatures.port(port)) for port in card.outputs]
    def param_type(type_):
        if type_.startswith("["):
            return f"Array[{param_type(type_[1:-1])}]"
        return {"bool": "bool", "long": "Integer", "double": "Float", "string": "String"}[type_]

    params = ", ".join(f"{quote(param.name)} => {param_type(param.type)}" for param in card.params)
    keys = [f"(:{port_symbol(name)} | {quote(name)})" for name, _ in inputs]
    fetch = [f"{key} -> {type_}" for key, (_, type_) in zip(keys, inputs)]
    fetch += [
        "(Symbol | String) -> untyped",
        "(Symbol | String, untyped) -> untyped",
        "(Symbol | String) { (String) -> untyped } -> untyped",
    ]
    lookup = [f"{key} -> {type_}?" for key, (_, type_) in zip(keys, inputs)]
    lookup.append("(Symbol | String) -> untyped")
    record = ", ".join(
        f"{'' if port.required else '?'}{port_symbol(port.name)}: {type_}{'' if port.required else '?'}"
        for port, type_ in outputs)
    rbs = [
        "# Generated by the Tsugumi Ruby SDK from this Card's schemas. Do not edit.",
        "# main(input) receives a Tsugumi::Inputs and returns a Tsugumi::output.",
        "module Tsugumi",
        '  type float = Float | "NaN" | "Infinity" | "-Infinity"',
        *(f"  type {alias} = {body}" for alias, body in signatures.aliases.items()),
        "",
        "  class Artifact",
        "    def self.dir: (?keep: bool) -> [Artifact, String]",
        "    def self.upload: (String, ?keep: bool) -> Artifact",
        "    def open: () -> String",
        "  end",
        "",
        f"  def self.params: () -> {{ {params} }}" if params else "  def self.params: () -> Hash[String, untyped]",
        "",
        "  class Inputs",
        "    # Needed input: raises Tsugumi::Failure (input_missing) when absent.",
        "    def fetch: " + "\n             | ".join(fetch),
        "    # Optional input: nil when absent.",
        "    def []: " + "\n          | ".join(lookup),
        "  end",
        "",
        f"  type output = {{ {record} }}" if record else "  type output = Hash[untyped, untyped]",
        "end",
        "",
    ]
    out.mkdir(parents=True, exist_ok=True)
    (out / "card.rbs").write_text("\n".join(rbs), encoding="utf-8")
    readme = out / "README.md"
    write_api_readme(
        readme,
        ((name, type_ + "?") for name, type_ in inputs),
        ((port.name, type_ + (" (required output)" if port.required else ""))
         for port, type_ in outputs),
        ((param.name, param_type(param.type)) for param in card.params),
    )
    with readme.open("a", encoding="utf-8") as file:
        file.write("\nSee `card.rbs` for Ruby types. Use `input.fetch(:port)` for a needed input "
                   "or `input[:port]` for an optional input.\n")


def port_symbol(name):
    return name if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name) else quote(name)


if __name__ == "__main__":
    schemas, out, reflection = (Path(path).resolve() for path in sys.argv[2:])
    card = load_card_schemas(schemas, reflection)
    (out / "include").mkdir(parents=True, exist_ok=True)
    names, output_schema = generate_headers(schemas, out, reflection)
    reflected = [reflect_schema(schemas / f"{name}.fbs", reflection) for name in SCHEMAS]
    (out / "src").mkdir(parents=True, exist_ok=True)
    (out / "src/card_glue.cc").write_text(generate_glue(card, reflected, names, output_schema), encoding="utf-8")
    generate_api(card, reflected, out / "api")
