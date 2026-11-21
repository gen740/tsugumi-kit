"""Generate typed FlatBuffers/JSON conversions from build-time schema metadata."""

import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
from card_cpp import KEYWORDS, ports

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


def quote(value):
    return json.dumps(value, ensure_ascii=False)


def ident(value):
    return value + "_" if value in KEYWORDS else value


def cpp(name):
    return "::" + "::".join(map(ident, name.split(".")))


def object_type(obj):
    return cpp(obj["name"]) + ("" if obj["is_struct"] else "T")


def field_type(kind, index, schema):
    if kind == "Obj":
        return object_type(schema["objects"][index])
    if index >= 0:
        return cpp(schema["enums"][index]["name"]) + ("Union" if kind == "Union" else "")
    return SCALARS[kind]


def emit_codec(kind, encode, decode):
    return [
        f"auto Codec<{kind}>::Encode(const {kind}& value) -> Json {{",
        *("  " + line for line in encode),
        "}",
        f"auto Codec<{kind}>::Decode(const Json& value) -> {kind} {{",
        *("  " + line for line in decode),
        "}",
        "",
    ]


def emit_object(obj, schema):
    fields = [
        f for f in sorted(obj["fields"], key=lambda f: f["id"])
        if not f["deprecated"] and f["type"]["base_type"] != "UType"
        and f["type"]["element"] != "UType"
    ]
    allowed = ", ".join(quote(f["name"]) for f in fields)
    if fields:
        encode = ["return Json{"]
    else:
        encode = ["(void)value;", "return Json::object();"]
    decode = [f"CheckObject(value, {{{allowed}}});"]
    kind = object_type(obj)
    if not obj["is_struct"]:
        decode.append(f"{kind} result;")
    arguments = []
    for field in fields:
        spec = field["type"]
        name = quote(field["name"])
        accessor = ident(field["name"])
        method = "ruby::Encode"
        member = f"value.{accessor}"
        if obj["is_struct"]:
            member += "()"
            if spec["base_type"] == "Array":
                method = "EncodeArray"
                member = "*" + member
                elem = field_type(spec["element"], spec["index"], schema)
                length = spec["fixed_length"]
                decode.append(f"const auto array_{accessor} = DecodeArray<{elem}, {length}>(value.at({name}));")
                arguments.append(f"flatbuffers::span<const {elem}, {length}>(array_{accessor}.data(), {length})")
            else:
                field_kind = field_type(spec["base_type"], spec["index"], schema)
                arguments.append(f"Codec<{field_kind}>::Decode(value.at({name}))")
        else:
            required = str(field["required"]).lower()
            decode.append(f"ReadMember(value, {name}, result.{accessor}, {required});")
        encode.append(f"  {{{name}, {method}({member})}},")
    if fields:
        encode.append("};")
    if obj["is_struct"]:
        decode.append(f"return {kind}({', '.join(arguments)});")
    else:
        decode.append("return result;")
    return emit_codec(kind, encode, decode)


def emit_enum(enum):
    kind = cpp(enum["name"])
    underlying = SCALARS[enum["underlying_type"]["base_type"]]
    encode = [
        f"if (value == {kind}::{ident(item['name'])}) return {quote(item['name'])};"
        for item in enum["values"]
    ]
    decode = [
        f"if (value == {quote(item['name'])}) return {kind}::{ident(item['name'])};"
        for item in enum["values"]
    ]
    encode.append(f"return static_cast<{underlying}>(value);")
    decode.append(f"return static_cast<{kind}>(Codec<{underlying}>::Decode(value));")
    return emit_codec(kind, encode, decode)


def emit_union(enum, schema):
    kind = cpp(enum["name"])
    native = kind + "Union"
    encode = ["switch (value.type) {"]
    decode = [
        "if (value.is_null()) return {};",
        "CheckObject(value, {\"type\", \"value\"});",
        "const auto& tag = value.at(\"type\");",
    ]
    for item in enum["values"]:
        spec = item["union_type"]
        tag = ident(item["name"])
        if spec["base_type"] == "None":
            encode.append(f"  case {kind}::{tag}: return nullptr;")
            continue
        member = field_type(spec["base_type"], spec["index"], schema)
        name = quote(item["name"])
        encode.extend([
            f"  case {kind}::{tag}:",
            f"    if (const auto* member = value.As{item['name']}())",
            f"      return Json{{{{\"type\", {name}}}, {{\"value\", ruby::Encode(*member)}}}};",
            "    break;",
        ])
        decode.extend([
            f"if (tag == {name}) {{",
            f"  {native} result;",
            f"  result.value = new {member}(Codec<{member}>::Decode(value.at(\"value\")));",
            f"  result.type = {kind}::{tag};",
            "  return result;",
            "}",
        ])
    encode.extend(["}", 'abi9::Reject(abi9::FailureKind::invalid_request, "invalid union");'])
    decode.append('throw std::invalid_argument("invalid union tag");')
    return emit_codec(native, encode, decode)


def run_card(contract):
    inputs = list(ports(contract["inputs"], {"Inputs", "params"}))
    outputs = list(ports(contract["outputs"], {"Output"}))
    lines = [
        "using abi9::FailureKind;",
        "using abi9::PortKind;",
        "using Tsugumi::Card::CardResultT;",
        "auto InvokeRuby(const Json& input) -> Json;",
        "auto FailureResult(const Json& failure) -> CardResultT;",
        "",
        "auto RunCard(tsugumi_bytes bytes) -> CardResultT {",
        "  abi9::Input input(bytes);",
        "  Json values = Json::object();",
    ]
    for name, _, table, kind, required in inputs:
        required = str(required).lower()
        lines += [
            f"  if (const auto port = input.ReadPort<{table}>({name}, {kind}, {required}))",
            f"    values[{name}] = Encode(*port);",
        ]
    lines += [
        "  input.CheckConsumed();",
        '  const auto params = abi9::ReadValue<::parameters::Params>(input.Params(), "params");',
        '  const auto reply = InvokeRuby(Json{{"ports", std::move(values)}, {"params", Encode(params)}});',
        '  if (reply.contains("failure")) return FailureResult(reply.at("failure"));',
        '  const auto& outputs = reply.at("outputs");',
        "  CardResultT result;",
        "  try {",
        f"    CheckObject(outputs, {{{', '.join(port[0] for port in outputs)}}});",
    ]
    for name, member, table, kind, required in outputs:
        required = str(required).lower()
        lines += [
            f"    std::optional<{table}T> output_{member};",
            f"    if (outputs.contains({name}))",
            f"      output_{member} = Codec<{table}T>::Decode(outputs.at({name}));",
            f"    abi9::WritePort<{table}>(result, {name}, {kind}, output_{member}, {required});",
        ]
    return lines + [
        "  } catch (const tsugumi::Failure&) {",
        "    throw;",
        "  } catch (const std::exception& error) {",
        "    abi9::Reject(FailureKind::output_invalid, error.what());",
        "  }",
        "  return result;",
        "}",
        "",
        "}  // namespace tsugumi::ruby",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi9::Invoke(call, result, tsugumi::ruby::RunCard);",
        "}",
        "",
    ]


def generate(contract, directory):
    lines = [
        "// Generated Ruby SDK glue. Do not edit.",
        '#include "adapter.hh"',
        '#include "json_codec.hh"',
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        "",
        "namespace tsugumi::ruby {",
        "",
    ]
    bodies = []
    codecs = set()
    for schema_name in ("_inputs", "_outputs", "_params"):
        metadata = (directory / f"{schema_name}.json").read_text()
        # flatc emits bare inf/nan for non-finite defaults even with strict JSON.
        # The object API handles defaults, so the generator can discard these.
        metadata = re.sub(r'("default_real":\s*)[-+]?(?:inf|nan)(?=[\s,}])',
                          r'\g<1>null', metadata)
        schema = json.loads(metadata)
        for obj in schema["objects"]:
            kind = object_type(obj)
            if kind not in codecs:
                codecs.add(kind)
                bodies.extend(emit_object(obj, schema))
        for enum in schema.get("enums", []):
            kind = cpp(enum["name"]) + ("Union" if enum["is_union"] else "")
            if kind not in codecs:
                codecs.add(kind)
                bodies.extend(emit_union(enum, schema) if enum["is_union"] else emit_enum(enum))
    for kind in sorted(codecs):
        lines.extend([
            f"template <> struct Codec<{kind}> {{",
            f"  static auto Encode(const {kind}& value) -> Json;",
            f"  static auto Decode(const Json& value) -> {kind};",
            "};",
        ])
    return "\n".join(lines + [""] + bodies + run_card(contract))


if __name__ == "__main__":
    metadata = Path(sys.argv[3])
    print(generate(json.loads(Path(sys.argv[2]).read_text()), metadata))
