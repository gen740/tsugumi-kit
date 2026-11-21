"""Emit typed C++ conversions. The schema metadata is used only in the build."""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(sys.argv[1]).parent))
from card_cpp import KEYWORDS, ports

# Flatc's C++ name escaping follows its own keyword list.
CPP_KEYWORDS = (KEYWORDS - {"char8_t", "consteval", "constinit"}) | {
    "atomic_cancel", "atomic_commit", "atomic_noexcept", "import", "module",
    "synchronized",
}

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


def identifier(name):
    return name + "_" if name in CPP_KEYWORDS else name


def qualified(name):
    return "::" + "::".join(map(identifier, name.split(".")))


def literal(name):
    return json.dumps(name)


class Schema:
    def __init__(self, schema):
        self.objects = schema["objects"]
        self.enums = schema["enums"]

    def type_for(self, typ, base):
        index = typ.get("index", -1)
        if base == "Obj":
            obj = self.objects[index]
            return qualified(obj["name"]) + ("" if obj.get("is_struct") else "T")
        if base == "Union":
            return qualified(self.enums[index]["name"]) + "Union"
        if index >= 0:
            return qualified(self.enums[index]["name"])
        return SCALARS[base]

    def object_codec(self, obj):
        is_struct = obj.get("is_struct", False)
        native = qualified(obj["name"]) + ("" if is_struct else "T")
        fields = [
            field for field in sorted(obj["fields"], key=lambda f: f.get("id", 0))
            if not field.get("deprecated")
            and "UType" not in (field["type"].get("base_type"), field["type"].get("element"))
        ]
        encode = []
        decode = [f"  CheckObject(value, {{{', '.join(literal(f['name']) for f in fields)}}});"]
        arguments = []
        if not is_struct:
            decode.append(f"  {native} result;")
        for field in fields:
            name = field["name"]
            key = literal(name)
            member = identifier(name)
            access = f"value.{member}" + ("()" if is_struct else "")
            typ = field["type"]
            base = typ["base_type"]
            elt = typ.get("element")
            operation = "julia::Encode"
            if base == "Array":
                element = self.type_for(typ, elt)
                storage = "uint8_t" if elt == "Bool" else element
                size = typ["fixed_length"]
                encode.append(f"  result[{key}] = EncodeArray<{element}>(*{access});")
                decode.append(
                    f"  const auto field_{member} = DecodeArray<{element}, {storage}, {size}>"
                    f"(Required(value, {key}));"
                )
                arguments.append(f"flatbuffers::span<const {storage}, {size}>(field_{member})")
                continue

            binary = base in ("Vector", "Vector64") and elt == "UByte" and typ.get("index", -1) < 0
            if binary:
                operation = "Base64::encode"
            encode.append(f"  result[{key}] = {operation}({access});")
            if is_struct:
                scalar = self.type_for(typ, base)
                default = f"{native}{{}}.{member}()"
                arguments.append(f"value.contains({key}) ? julia::Decode<{scalar}>(Required(value, {key})) : {default}")
                continue

            target = f"result.{member}"
            conversion = (
                f"Base64::decode({{value}}.get<std::string>())" if binary
                else f"julia::Decode<decltype({target})>({{value}})"
            )
            if field.get("required"):
                decode.append(f"  {target} = {conversion.format(value=f'Required(value, {key})')};")
            else:
                decode += [
                    f"  if (const auto field = value.find({key}); field != value.end() && !field->is_null())",
                    f"    {target} = {conversion.format(value='(*field)')};",
                ]

        if is_struct:
            decode.append(f"  return {native}({', '.join(arguments)});" if arguments else f"  return {native}{{}};")
        else:
            decode.append("  return result;")

        return native, ["  auto result = Json::object();", *encode, "  return result;"], decode

    def enum_codec(self, enum):
        native = qualified(enum["name"])
        if not enum.get("is_union"):
            encode, decode = [], []
            for variant in enum["values"]:
                name = literal(variant["name"])
                tag = f"{native}::{identifier(variant['name'])}"
                encode.append(f"  if (value == {tag}) return {name};")
                decode.append(f"  if (value == {name}) return {tag};")
            raw = f"std::underlying_type_t<{native}>"
            encode.append(f"  return julia::Encode(static_cast<{raw}>(value));")
            decode.append(f"  return static_cast<{native}>(julia::Decode<{raw}>(value));")
            return native, encode, decode

        union = native + "Union"
        encode = ["  switch (value.type) {"]
        decode = [
            "  if (value.is_null()) return {};",
            '  CheckObject(value, {"type", "value"});',
            '  const auto& type = Required(value, "type");',
            f"  {union} result;",
        ]
        for variant in enum["values"]:
            typ = variant.get("union_type", {})
            member = variant["name"]
            name = literal(member)
            tag = f"{native}::{identifier(member)}"
            base = typ.get("base_type", "None")
            if base == "None":
                encode.append(f"    case {tag}: return nullptr;")
                continue

            scalar = self.type_for(typ, base)
            encode += [
                f"    case {tag}:",
                f"      if (const auto* data = value.As{identifier(member)}())",
                f'        return Json{{{{"type", {name}}}, {{"value", julia::Encode(*data)}}}};',
                "      break;",
            ]
            decode += [
                f"  if (type == {name}) {{",
                f'    result.value = new {scalar}(julia::Decode<{scalar}>(Required(value, "value")));',
                f"    result.type = {tag};",
                "    return result;",
                "  }",
            ]

        encode += ["  }", '  throw std::invalid_argument("Invalid input union");']
        decode.append('  throw std::invalid_argument("Unknown union member");')
        return union, encode, decode


def generate(contract, schemas):
    declarations = [
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        '#include "codec.hh"',
        "",
        "namespace tsugumi::julia {",
        "",
    ]
    definitions = []
    seen = set()

    for schema in schemas:
        for element, codec in [
            *((obj, schema.object_codec) for obj in schema.objects),
            *((enum, schema.enum_codec) for enum in schema.enums),
        ]:
            native, encode, decode = codec(element)
            if native in seen:
                continue
            seen.add(native)
            declarations += [
                "template <>",
                f"struct Codec<{native}> {{",
                f"  static auto Encode(const {native}& value) -> Json;",
                f"  static auto Decode(const Json& value) -> {native};",
                "};",
            ]
            definitions += [
                f"auto Codec<{native}>::Encode([[maybe_unused]] const {native}& value) -> Json {{",
                *encode,
                "}",
                f"auto Codec<{native}>::Decode(const Json& value) -> {native} {{",
                *decode,
                "}",
            ]

    body = [
        "namespace {",
        "using namespace tsugumi::abi9;",
        "",
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  Input input(bytes);",
        "  auto members = Json::object();",
        "  std::string request;",
        "  try {",
    ]
    for name, _, table, kind, required in ports(contract["inputs"], {"Inputs", "params"}):
        body.append(f"    members[{name}] = Encode(input.ReadPort<{table}>({name}, {kind}, {str(required).lower()}));")
    body += [
        "    input.CheckConsumed();",
        '    const auto params = Encode(ReadValue<::parameters::Params>(input.Params(), "params"));',
        '    request = Json{{"ports", members}, {"params", params}}.dump();',
        "  } catch (const Json::exception& error) {",
        "    Reject(FailureKind::invalid_request, error.what());",
        "  } catch (const std::invalid_argument& error) {",
        "    Reject(FailureKind::invalid_request, error.what());",
        "  }",
        "  const auto response = RunJulia(request);",
        '  if (response.contains("failure")) return ReadFailure(response.at("failure"));',
        "  Tsugumi::Card::CardResultT result;",
        "  try {",
        '    const auto& output = response.at("output");',
    ]
    outputs = list(ports(contract["outputs"], {"Output"}))
    body.append(f"    CheckObject(output, {{{', '.join(port[0] for port in outputs)}}});")
    for name, member, table, kind, required in outputs:
        body += [
            f"    const auto field_{member} = output.find({name});",
            f"    const auto value_{member} = field_{member} == output.end() ? std::nullopt",
            f"        : Decode<std::optional<{table}T>>(*field_{member});",
            f"    WritePort<{table}>(result, {name}, {kind}, value_{member}, {str(required).lower()});",
        ]
    body += [
        "  } catch (const Json::exception& error) {",
        "    Reject(FailureKind::output_invalid, error.what());",
        "  } catch (const std::invalid_argument& error) {",
        "    Reject(FailureKind::output_invalid, error.what());",
        "  } catch (const cppcodec::parse_error& error) {",
        "    Reject(FailureKind::output_invalid, error.what());",
        "  }",
        "  return result;",
        "}",
        "}  // namespace",
        "}  // namespace tsugumi::julia",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi9::Invoke(call, result, tsugumi::julia::RunCard);",
        "}",
    ]
    return "\n".join(declarations + definitions + body) + "\n"


if __name__ == "__main__":
    contract = json.loads(Path(sys.argv[2]).read_text())
    schemas = [Schema(json.loads(Path(path).read_text())) for path in sys.argv[3:]]
    sys.stdout.write(generate(contract, schemas))
