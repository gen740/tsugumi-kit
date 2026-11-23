"""Emit the C++ glue between the Card ABI and a Julia Card, plus its API README.

The ports come from the shared reflection module. The C++ bridge uses the
C++ SDK's object API headers, with the same reserved-name normalization.
"""

import json
import shutil
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

if sys.argv[1] == "--params":
    values = json.loads(Path(sys.argv[2]).read_text())

    def julia_value(value):
        if type(value) is int:
            return "typemin(Int64)" if value == -(1 << 63) else f"Int64({value})"
        if isinstance(value, list):
            return "(" + ", ".join(map(julia_value, value)) + ",)"
        return json.dumps(value, ensure_ascii=False).replace("$", r"\$")

    declarations = (f"Symbol({julia_value(key)}) => {julia_value(value)}"
                    for key, value in values.items())
    Path(sys.argv[3]).write_text("const params = (; " + ", ".join(declarations) + ")\n")
    sys.exit(0)


sys.path.insert(0, sys.argv[1])
from card_cpp import (
    cpp_type, flatc_identifier, generate_headers,
    normalize_schema, schema_tokens, validators,
)
from tsugumi_ports import load_card_schemas, reflect_schema, write_api_readme

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


def qualified(name):
    return "::" + name.replace(".", "::")


def literal(name):
    return json.dumps(name)


class Schema:
    def __init__(self, schema, original, names):
        self.objects = schema["objects"]
        self.enums = schema["enums"]
        def normalized_name(name):
            return ".".join(names.get(part, part) for part in name.split("."))

        self.original_fields = {
            normalized_name(obj["name"]): {
                field.get("id", 0): field["name"] for field in obj["fields"]
            }
            for obj in original["objects"]
        }
        self.original_variants = {
            normalized_name(enum["name"]): {
                variant.get("value", 0): variant["name"] for variant in enum["values"]
            }
            for enum in original["enums"]
        }

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
        if obj["name"] == "tsugumi.Artifact":
            return native, [
                '  return Json{{"$artifact", Base64::encode(value.id->bytes()->Data(), 16)}};',
            ], [
                '  const auto bytes = Base64::decode(Required(value, "$artifact").get<std::string>());',
                '  if (bytes.size() != 16) throw std::invalid_argument("Expected an Artifact ID");',
                '  tsugumi::ArtifactT result;',
                '  result.id = std::make_unique<tsugumi::ArtifactId>(',
                '      flatbuffers::span<const uint8_t, 16>(bytes.data(), 16));',
                '  return result;',
            ]
        fields = [
            field for field in sorted(obj["fields"], key=lambda f: f.get("id", 0))
            if not field.get("deprecated")
            and "UType" not in (field["type"].get("base_type"), field["type"].get("element"))
        ]
        encode = []
        original = self.original_fields[obj["name"]]
        keys = [literal(original[field.get("id", 0)]) for field in fields]
        decode = [f"  CheckObject(value, {{{', '.join(keys)}}});"]
        arguments = []
        if not is_struct:
            decode.append(f"  {native} result;")
        for field, key in zip(fields, keys):
            member = flatc_identifier(field["name"])
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
        original = self.original_variants[enum["name"]]
        if not enum.get("is_union"):
            encode, decode = [], []
            for variant in enum["values"]:
                name = literal(original[variant.get("value", 0)])
                tag = f"{native}::{flatc_identifier(variant['name'])}"
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
            member = flatc_identifier(variant["name"])
            name = literal(original[variant.get("value", 0)])
            tag = f"{native}::{member}"
            base = typ.get("base_type", "None")
            if base == "None":
                encode.append(f"    case {tag}: return nullptr;")
                continue

            scalar = self.type_for(typ, base)
            encode += [
                f"    case {tag}:",
                f"      if (const auto* data = value.As{member}())",
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

def merge_tags(members):
    """Use a qualified member type when short names collide."""
    short = [member.type.rsplit(".", 1)[-1] for member in members]
    return [
        name if short.count(name) == 1
        else member.type
        for name, member in zip(short, members)
    ]


def native(type_, names):
    return f"{cpp_type(type_, names)}::NativeTableType"


def julia_type(port):
    if len(port.candidates) > 1:
        return " | ".join(f'(type="{tag}", value=Dict of {member.type})'
                          for member, tag in zip(port.candidates, merge_tags(port.candidates)))
    return "Artifact" if port.type == "tsugumi.Artifact" else f"Dict{{String,Any}} of {port.type}"


def generate(card, schemas, names, output_schema):
    declarations = [
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
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
            native_name, encode, decode = codec(element)
            if native_name in seen:
                continue
            seen.add(native_name)
            declarations += [
                "template <>",
                f"struct Codec<{native_name}> {{",
                f"  static auto Encode(const {native_name}& value) -> Json;",
                f"  static auto Decode(const Json& value) -> {native_name};",
                "};",
            ]
            definitions += [
                f"auto Codec<{native_name}>::Encode([[maybe_unused]] const {native_name}& value) -> Json {{",
                *encode,
                "}",
                f"auto Codec<{native_name}>::Decode(const Json& value) -> {native_name} {{",
                *decode,
                "}",
            ]

    read = []
    for port in card.inputs:
        name = json.dumps(port.name)
        read += [f"    if (const auto entry = input.ReadPort({name})) {{"]
        for index, (member, tag) in enumerate(zip(port.candidates, merge_tags(port.candidates))):
            value = f"Encode(ReadValue<{cpp_type(member.type, names)}>(entry->bytes, {name}))"
            if len(port.candidates) > 1:
                value = f'Json{{{{"type", {json.dumps(tag)}}}, {{"value", {value}}}}}'
            keyword = "if" if index == 0 else "else if"
            read += [f'      {keyword} (entry->schema == "{member.schema}")',
                     f"        members[{name}] = {value};"]
        read += [f'      else Reject(FailureKind::input_invalid, "Unknown schema for " + std::string({name}));',
                 '    } else {',
                 f'      Reject(FailureKind::input_missing, "Missing input " + std::string({name}));',
                 '    }']

    write = [f"    CheckObject(output, {{{', '.join(json.dumps(port.name) for port in card.outputs)}}});"]
    for index, port in enumerate(card.outputs):
        name = json.dumps(port.name)
        required = str(port.required).lower()
        field = f"field_{index}"
        present = f"{field} != output.end() && !{field}->is_null()"
        write.append(f"    const auto {field} = output.find({name});")
        table = cpp_type(port.type, names)
        write += [f"    std::optional<{native(port.type, names)}> value_{index};",
                  f"    if ({present}) value_{index} = Decode<{native(port.type, names)}>(*{field});",
                  f"    WritePort<{table}>(result, {name}, value_{index}, {required}, VisitArtifacts);"]

    roots = (cpp_type(port.type, names) for port in card.outputs)
    body = [
        "namespace {",
        "using namespace tsugumi::abi;",
        "",
        *validators(output_schema, roots),
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  Input input(bytes);",
        "  auto members = Json::object();",
        "  std::string request;",
        "  try {",
        *read,
        "    input.CheckConsumed();",
        '    request = Json{{"ports", members}}.dump();',
        "  } catch (const Json::exception& error) {",
        "    Reject(FailureKind::input_invalid, error.what());",
        "  } catch (const std::invalid_argument& error) {",
        "    Reject(FailureKind::input_invalid, error.what());",
        "  }",
        "  const auto response = RunJulia(request);",
        '  if (response.contains("failure")) return ReadFailure(response.at("failure"));',
        "  Tsugumi::Card::CardResultT result;",
        "  try {",
        '    const auto& output = response.at("output");',
        *write,
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
        "  return tsugumi::abi::Invoke(call, result, tsugumi::julia::RunCard);",
        "}",
    ]
    return "\n".join(declarations + definitions + body) + "\n"


API_NOTES = """
## Julia values

`main(input)` reads `input.port` (or `input[:port]`) for an input it needs;
an absent input raises `CardFailure(:input_missing, "Missing input port")`.
`get(input, :port, nothing)` returns `nothing` for an absent input.
Read parameters from the generated `params` constant. Return a `NamedTuple` or a dictionary keyed by output port.

A table is a dictionary keyed by the field names in the `.fbs` files in this
directory. A mixed Merge is a dictionary with `"type"` and `"value"` keys.
Artifact fields hold `Artifact` values and Collect holds opaque envelopes.
"""


if __name__ == "__main__":
    input_api, source, out, reflection = (Path(path).resolve() for path in sys.argv[2:])
    card = load_card_schemas(source, reflection)
    (out / "include").mkdir(parents=True, exist_ok=True)
    (out / "src").mkdir(parents=True, exist_ok=True)
    names, output_schema = generate_headers(source, out, reflection)
    with TemporaryDirectory() as directory:
        # The field IDs and numeric enum values recover the exact Julia keys
        # and tags from the original schemas, even when flatc names change.
        normalized = Path(directory)
        for path in source.rglob("*.fbs"):
            if path.name.startswith("._"):
                continue
            target = normalized / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            text = path.read_text(encoding="utf-8")
            target.write_text(normalize_schema(text, schema_tokens(text), names))
        schemas = [
            Schema(
                output_schema if base == "_outputs"
                else reflect_schema(normalized / f"{base}.fbs", reflection),
                reflect_schema(source / f"{base}.fbs", reflection),
                names,
            )
            for base in SCHEMAS
        ]
    (out / "src/card_adapter.cc").write_text(generate(card, schemas, names, output_schema))
    api = out / "api"
    api.mkdir()
    write_api_readme(
        api / "README.md",
        ((port.name, julia_type(port) + " or nothing") for port in card.inputs),
        ((port.name, julia_type(port) + (" (required output)" if port.required else ""))
         for port in card.outputs),
        ((param.name, param.type) for param in card.params),
    )
    with (api / "README.md").open("a", encoding="utf-8") as readme:
        readme.write(API_NOTES)
    overview = (api / "README.md").read_text(encoding="utf-8")
    (api / "card.jl").write_text(
        "\n".join(f"# {line}".rstrip() for line in overview.splitlines())
        + "\n\n" + input_api.read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    for schema in source.rglob("*.fbs"):
        target = api / schema.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(schema, target)
