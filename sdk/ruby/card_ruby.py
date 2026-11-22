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

sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])

from card_cpp import FLATC_KEYWORDS, cpp_type, flatc_identifier, generate_headers  # noqa: E402
from tsugumi_ports import fields, load_card_schemas, reflect_schema, write_api_readme  # noqa: E402

SCHEMAS = ("_inputs", "_outputs", "_params")
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
        self.functions = []

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

    def decode_merge(self, type_, members):
        """Return a function that reads the selected box as a tagged Ruby value."""
        function = f"DecodeMerge{len(self.functions)}"
        body = [f"auto {function}(const {self.cxx(type_)}T& merged, std::string_view name) -> Json {{"]
        self.functions.append(body)
        for member, ruby_name in zip(members, merge_names(members)):
            target = self.cxx(member.type)
            value = f"Encode(abi::ReadValue<{target}>({{box->bytes.data(), box->bytes.size()}}, name))"
            if member.merge:
                child = self.decode_merge(member.type, member.merge)
                value = f"{child}(abi::ReadValue<{target}>({{box->bytes.data(), box->bytes.size()}}, name), name)"
            body += [f"  if (const auto* box = merged.value.As{self.variant(member.tag)}())",
                     f'    return Json{{{{"type", {quote(ruby_name)}}}, {{"value", {value}}}}};']
        body += ['  abi::Reject(FailureKind::invalid_request, "Invalid Merge input " + std::string(name));',
                 "}", ""]
        return function

    def encode_merge(self, type_, members):
        """Return a function that boxes a tagged Ruby value."""
        function = f"EncodeMerge{len(self.functions)}"
        merged = self.cxx(type_) + "T"
        body = [f"auto {function}(const Json& value, std::string_view name) -> {merged} {{",
                '  CheckObject(value, {"type", "value"});',
                '  const auto& tag = value.at("type");',
                f"  {merged} merged;"]
        self.functions.append(body)
        for member, ruby_name in zip(members, merge_names(members)):
            target = self.cxx(member.type)
            native = f'Codec<{target}T>::Decode(value.at("value"))'
            if member.merge:
                native = f'{self.encode_merge(member.type, member.merge)}(value.at("value"), name)'
            body += [f"  if (tag == {quote(ruby_name)}) {{",
                     f"    {self.cxx(member.box_type)}T box;",
                     f"    box.bytes = abi::PackValue<{target}>({native}, name);",
                     "    merged.value.Set(std::move(box));",
                     "    return merged;",
                     "  }"]
        body += ['  throw std::invalid_argument("invalid Merge member for " + std::string(name));',
                 "}", ""]
        return function


def run_card(card, glue):
    cxx = glue.cxx
    lines = [
        "auto RunCard(tsugumi_bytes bytes) -> CardResultT {",
        "  abi::Input input(bytes);",
        "  Json values = Json::object();",
    ]
    for port in card.inputs:
        name = quote(port.name)
        if port.kind == "Path":
            lines += [f"  if (const auto path = input.ReadPath({name}))",
                      f"    values[{name}] = path->string();"]
            continue
        value = "Encode(*port)"
        if port.merge:
            value = f"{glue.decode_merge(port.type, port.merge)}(*port, {name})"
        lines += [f"  if (const auto port = input.ReadPort<{cxx(port.type)}>({name}))",
                  f"    values[{name}] = {value};"]
    lines += [
        "  input.CheckConsumed();",
        f'  const auto params = abi::ReadValue<{cxx(card.params_type)}>(input.Params(), "params");',
        '  const auto reply = InvokeRuby(Json{{"ports", std::move(values)}, {"params", Encode(params)}});',
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
        if port.kind == "Path":
            native = "std::filesystem::path"
            decode = "value->get<std::string>()"
        elif port.merge:
            native = table + "T"
            decode = f"{glue.encode_merge(port.type, port.merge)}(*value, {name})"
        else:
            native = table + "T"
            decode = f"Codec<{table}T>::Decode(*value)"
        lines += [
            f"    std::optional<{native}> output_{index};",
            f"    if (const auto value = outputs.find({name}); value != outputs.end() && !value->is_null())",
            f"      output_{index} = {decode};",
            (f"    abi::WritePath(result, {name}, output_{index}, {required});" if port.kind == "Path"
             else f"    abi::WritePort<{table}>(result, {name}, output_{index}, {required});"),
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


def generate_glue(card, schemas, names):
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
        '#include "_params_generated.h"',
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
        *(line for function in reversed(glue.functions) for line in function),
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

    def merge(self, members):
        return " | ".join(
            f'{{ "type" => {quote(name)}, "value" => '
            f'{"(" + self.merge(member.merge) + ")" if member.merge else self.table(member.type)} }}'
            for member, name in zip(members, merge_names(members)))

    def port(self, port, *, output=False):
        if port.kind == "Path":
            return "(String | _PathLike)" if output else "String"
        return f"({self.merge(port.merge)})" if port.merge else self.table(port.type)


def generate_api(card, schemas, out):
    signatures = Signatures(schemas)
    inputs = [(port.name, signatures.port(port)) for port in card.inputs]
    outputs = [(port, signatures.port(port, output=True)) for port in card.outputs]
    params = signatures.table(card.params_type)
    _, param_schema = signatures.objects[card.params_type]
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
        "  interface _PathLike",
        "    def to_path: () -> String",
        "  end",
        "",
        "  class Inputs",
        "    # Needed input: raises Tsugumi::Failure (input_missing) when absent.",
        "    def fetch: " + "\n             | ".join(fetch),
        "    # Optional input: nil when absent.",
        "    def []: " + "\n          | ".join(lookup),
        f"    def params: () -> {params}",
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
        ((param.name, signatures.field(param.field, param_schema)) for param in card.params),
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
    names, _ = generate_headers(schemas, out, reflection)
    reflected = [reflect_schema(schemas / f"{name}.fbs", reflection) for name in SCHEMAS]
    (out / "src").mkdir(parents=True, exist_ok=True)
    (out / "src/card_glue.cc").write_text(generate_glue(card, reflected, names), encoding="utf-8")
    generate_api(card, reflected, out / "api")
