"""Emit typed C++ conversions. The schema metadata is used only in the build."""

import json
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(sys.argv[1]).parent))
from card_cpp import (
    SCHEMAS, flatc_identifier, generate_headers, normalize_schema,
    ports, schema_tokens, validators,
)

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


def generate(contract, schemas, names, output_schema):
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

    outputs = list(ports(contract["outputs"], {"Output"}, names))
    body = [
        "namespace {",
        "using namespace tsugumi::abi9;",
        "",
        *validators(output_schema, (table for _, _, table, _, _ in outputs)),
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  Input input(bytes);",
        "  auto members = Json::object();",
        "  std::string request;",
        "  try {",
    ]
    for name, _, table, kind, required in ports(contract["inputs"], {"Inputs", "params"}, names):
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
    body.append(f"    CheckObject(output, {{{', '.join(port[0] for port in outputs)}}});")
    for name, member, table, kind, required in outputs:
        body += [
            f"    const auto field_{member} = output.find({name});",
            f"    const auto value_{member} = field_{member} == output.end() ? std::nullopt",
            f"        : Decode<std::optional<{table}T>>(*field_{member});",
            f"    WritePort<{table}>(result, {name}, {kind}, value_{member}, {str(required).lower()}, IsPackable);",
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


def reflect(schema, root, out, reflection):
    subprocess.run(["flatc", "-b", "--schema", "-o", str(out), f"{schema}.fbs"],
                   cwd=root, check=True)
    subprocess.run(["flatc", "-t", "--strict-json", str(reflection), "--", f"{schema}.bfbs"],
                   cwd=out, check=True)
    return json.loads((out / f"{schema}.json").read_text())


if __name__ == "__main__":
    contract_file, out, reflection = (Path(path).resolve() for path in sys.argv[2:])
    root = contract_file.parent
    (out / "include").mkdir(parents=True, exist_ok=True)
    names, output_schema = generate_headers(root, out, reflection)
    with TemporaryDirectory() as directory:
        work = Path(directory)
        # The wire IDs and numeric enum values let us recover the exact Julia
        # keys and tags from the original schemas, even when flatc names change.
        normalized = work / "normalized"
        for path in root.rglob("*.fbs"):
            if path.name.startswith("._"):
                continue
            target = normalized / path.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            source = path.read_text(encoding="utf-8")
            target.write_text(normalize_schema(source, schema_tokens(source), names))
        schemas = []
        for base in SCHEMAS:
            original = reflect(base, root, work, reflection)
            schema = output_schema if base == "_outputs" else reflect(
                base, normalized, work, reflection
            )
            schemas.append(Schema(schema, original, names))
        contract = json.loads(contract_file.read_text())
        sys.stdout.write(generate(contract, schemas, names, output_schema))
