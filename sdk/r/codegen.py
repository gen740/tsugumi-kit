"""Generate the R Card's C++ JSON codecs, port glue and API README.

The ports come from the shared reflection module; the codecs follow the
reflected tables of the same schemas.
"""

import json
import shutil
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, reflect_schema, write_api_readme

# flatc appends an underscore to object API members named like C++ keywords.
sys.path.insert(0, sys.argv[1])
from card_cpp import FLATC_KEYWORDS

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


def cpp(name):
    return "::" + name.replace(".", "::")


def quote(text):
    return json.dumps(text, ensure_ascii=False)


class Codecs:
    def __init__(self, schemas, keywords):
        self.keywords = keywords
        self.types = {}
        self.bodies = []
        for schema in schemas:
            for entry in schema["objects"] + schema["enums"]:
                self.types.setdefault(entry["name"], (entry, schema))
        self.used = {}

    def use(self, name):
        if name in self.used:
            return self.used[name][0]
        entry, schema = self.types[name]
        native = cpp(name)
        if entry.get("is_union"):
            native += "Union"
        elif "fields" in entry and not entry.get("is_struct"):
            native += "T"
        index = self.used[name] = (len(self.used), native)
        if entry.get("is_union"):
            to, from_ = self.union(entry, schema, native)
        elif "values" in entry:
            to, from_ = self.enum(entry, native)
        elif entry.get("is_struct"):
            to, from_ = self.struct(entry, schema, native)
        else:
            to, from_ = self.table(entry, schema, native)
        number = index[0]
        self.bodies.append(
            f"auto To{number}([[maybe_unused]] const {native}& value) -> Json {{\n"
            f"{to}\n}}\n"
            f"auto From{number}([[maybe_unused]] const Json& json) -> {native} {{\n"
            f"{from_}\n}}\n"
        )
        return number

    def type_name(self, type_, schema):
        index = type_.get("index", -1)
        if index == -1:
            return None
        category = "objects" if type_["base_type"] == "Obj" else "enums"
        return schema[category][index]["name"]

    def convert(self, type_, schema, direction, expr):
        name = self.type_name(type_, schema)
        if name:
            index = self.use(name)
            return f"{direction}{index}({expr})"
        if direction == "To":
            scalar = SCALARS[type_["base_type"]]
            return f"ToValue<{scalar}>({expr})"
        return f'FromValue<{SCALARS[type_["base_type"]]}>({expr})'

    def element(self, type_):
        return dict(type_, base_type=type_["element"])

    def numeric(self, type_, schema):
        base = type_["base_type"]
        return (base in SCALARS and base not in ("Bool", "String")
                and not self.type_name(type_, schema))

    def field(self, field):
        name = field["name"]
        return name + "_" if name in self.keywords else name

    def table(self, entry, schema, native):
        to = ["  Json json = Json::object();"]
        from_ = [f"  {native} value;", "  if (!json.is_object()) throw std::invalid_argument(\"Expected a table object\");"]
        for field in entry["fields"]:
            if field.get("deprecated"):
                continue
            type_ = field["type"]
            base = type_["base_type"]
            if base == "UType" or (base == "Vector" and type_["element"] == "UType"):
                continue  # The discriminator is carried alongside each union value.
            name = quote(field["name"])
            member = "value." + self.field(field)
            optional = base == "Obj" or (base in SCALARS and base != "String" and field.get("optional"))
            value = "*" + member if optional else member
            converted = self.to_field(type_, schema, value)
            if optional:
                converted = f"{member} ? {converted} : Json(nullptr)"
            to.append(f"  json[{name}] = {converted};")

            converted = self.from_field(type_, schema, "*item")
            if base == "Obj":
                object_name = self.type_name(type_, schema)
                _, target = self.used[object_name]
                converted = f"std::make_unique<{target}>({converted})"
            from_ += [
                f"  if (const auto item = json.find({name}); item != json.end() && !item->is_null()) {{",
                f"    {member} = {converted};",
                "  }",
            ]
            if field.get("required"):
                from_[-1] = ('  } else { throw std::invalid_argument('
                             f'{quote("Missing required field " + field["name"])}); }}')
        return "\n".join(to + ["  return json;"]), "\n".join(from_ + ["  return value;"])

    def to_field(self, type_, schema, value):
        if type_["base_type"] != "Vector":
            return self.convert(type_, schema, "To", value)
        element = self.element(type_)
        target = self.type_name(element, schema)
        if element["base_type"] == "UByte" and not target:
            return f"ToBase64({value})"
        dereference = target and element["base_type"] == "Obj" and not self.types[target][0].get("is_struct")
        converted = self.convert(element, schema, "To", "*item" if dereference else "item")
        array = "ToArray<true>" if self.numeric(element, schema) else "ToArray"
        return f"{array}({value}, [](const auto& item) {{ return {converted}; }})"

    def from_field(self, type_, schema, value):
        if type_["base_type"] != "Vector":
            return self.convert(type_, schema, "From", value)
        element = self.element(type_)
        target = self.type_name(element, schema)
        if element["base_type"] == "UByte" and not target:
            return f"FromBase64({value})"
        converted = self.convert(element, schema, "From", "item")
        if target and element["base_type"] == "Obj" and not self.types[target][0].get("is_struct"):
            converted = f"std::make_unique<{self.used[target][1]}>({converted})"
        vector = "ReadVector<true>" if self.numeric(element, schema) else "ReadVector"
        return f"{vector}({value}, [](const Json& item) {{ return {converted}; }})"

    def struct(self, entry, schema, native):
        to = ["  Json json = Json::object();"]
        from_ = []
        args = []
        for field in sorted(entry["fields"], key=lambda field: field.get("id", 0)):
            type_ = field["type"]
            name = quote(field["name"])
            key = self.field(field)
            source = f"Member(json, {name})"
            value = f"value.{key}()"
            if type_["base_type"] == "Array":
                element = self.element(type_)
                target = self.type_name(element, schema)
                length = type_["fixed_length"]
                if element["base_type"] == "Obj":
                    item_type = cpp(target)
                    item = "*item"
                else:
                    item_type = cpp(target) if target else SCALARS[element["base_type"]]
                    item = "item"
                    if item_type == "bool":
                        item_type = "uint8_t"
                convert = self.convert(element, schema, "To", item)
                numeric = self.numeric(element, schema)
                array = "ToArray<true>" if numeric else "ToArray"
                to.append(f"  json[{name}] = {array}(*{value}, [](const auto& item) {{ return {convert}; }});")
                convert = self.convert(element, schema, "From", "item")
                allow_scalar = ", true" if numeric and length == 1 else ""
                from_.append(f"  const auto array_{key} = ReadArray<{item_type}, {length}{allow_scalar}>({source}, [](const Json& item) {{ return static_cast<{item_type}>({convert}); }});")
                args.append(f"flatbuffers::span<const {item_type}, {length}>(array_{key})")
            else:
                to.append(f'  json[{name}] = {self.convert(type_, schema, "To", value)};')
                args.append(self.convert(type_, schema, "From", source))
        return "\n".join(to + ["  return json;"]), "\n".join(from_ + [
            f"  return {native}({', '.join(args)});"
        ])

    def enum(self, entry, native):
        storage = SCALARS[entry["underlying_type"]["base_type"]]
        to = []
        from_ = []
        for member in entry["values"]:
            tag = f'{native}::{member["name"]}'
            name = quote(member["name"])
            to.append(f"  if (value == {tag}) return {name};")
            from_.append(f"  if (json == {name}) return {tag};")
        to.append(f"  return ToValue(static_cast<{storage}>(value));")
        from_.append(f"  return static_cast<{native}>(ReadInteger<{storage}>(json));")
        return "\n".join(to), "\n".join(from_)

    def union(self, entry, schema, native):
        union = cpp(entry["name"])
        to = [f"  if (value.type == {union}::NONE) return nullptr;"]
        from_ = [f"  {native} value;", "  if (json.is_null()) return value;",
                 '  const auto& tag = Member(json, "type");',
                 '  const auto& payload = Member(json, "value");']
        for member in entry["values"]:
            if member["name"] == "NONE":
                continue
            name = quote(member["name"])
            tag = f'{union}::{member["name"]}'
            type_ = member["union_type"]
            target = self.type_name(type_, schema)
            if target:
                self.use(target)
                object_type = self.used[target][1]
            else:
                object_type = SCALARS[type_["base_type"]]
            to += [
                f"  if (value.type == {tag}) {{",
                f"    if (const auto* item = value.As{member['name']}())",
                '      return Json{{"type", ' + name + '}, {"value", ' +
                self.convert(type_, schema, "To", "*item") + '}};',
                "  }",
            ]
            from_ += [
                f"  if (tag == {name}) {{",
                f'    value.value = new {object_type}({self.convert(type_, schema, "From", "payload")});',
                f"    value.type = {tag};",
                "    return value;",
                "  }",
            ]
        to += ['  throw std::invalid_argument("Invalid union value");']
        from_ += ['  throw std::invalid_argument("Unknown union tag");']
        return "\n".join(to), "\n".join(from_)

    def emit(self):
        declarations = [
            f"[[maybe_unused]] auto To{i}(const {native}& value) -> Json;\n"
            f"[[maybe_unused]] auto From{i}(const Json& json) -> {native};"
            for i, native in self.used.values()
        ]
        return "\n\n".join(["\n".join(declarations)] + self.bodies)


def merge_tags(members):
    """Use a qualified member type when short names collide."""
    short = [member.type.rsplit(".", 1)[-1] for member in members]
    return [
        name if short.count(name) == 1
        else member.type
        for name, member in zip(short, members)
    ]


def native(name):
    return f"{cpp(name)}::NativeTableType"


class Ports:
    """Emit the Merge conversions that hide the core's boxes from R."""

    def __init__(self, codecs):
        self.codecs = codecs
        self.functions = []

    def decode_merge(self, type_, members):
        function = f"DecodeMerge{len(self.functions)}"
        body = [f"auto {function}(const {native(type_)}& merged, std::string_view name) -> Json {{"]
        self.functions.append(body)
        for member, tag in zip(members, merge_tags(members)):
            value = f"ReadValue<{cpp(member.type)}>({{box->bytes.data(), box->bytes.size()}}, name)"
            if member.merge:
                value = f"{self.decode_merge(member.type, member.merge)}({value}, name)"
            else:
                value = f"To{self.codecs.use(member.type)}({value})"
            body += [f"  if (const auto* box = merged.value.As{member.tag}())",
                     f"    return Json{{{{\"type\", {quote(tag)}}}, {{\"value\", {value}}}}};"]
        body += ['  Reject(FailureKind::invalid_request, "Invalid Merge input " + std::string(name));',
                 "}"]
        return function

    def encode_merge(self, type_, members):
        function = f"EncodeMerge{len(self.functions)}"
        body = [
            f"auto {function}(const Json& value, std::string_view name) -> {native(type_)} {{",
            '  const auto& type = Member(value, "type");',
            '  const auto& payload = Member(value, "value");',
            f"  {native(type_)} merged;",
        ]
        self.functions.append(body)
        for member, tag in zip(members, merge_tags(members)):
            if member.merge:
                value = f"{self.encode_merge(member.type, member.merge)}(payload, name)"
            else:
                value = f"From{self.codecs.use(member.type)}(payload)"
            body += [f"  if (type == {quote(tag)}) {{",
                     f"    {native(member.box_type)} box;",
                     f"    box.bytes = PackValue<{cpp(member.type)}>({value}, name);",
                     "    merged.value.Set(std::move(box));",
                     "    return merged;",
                     "  }"]
        body += ['  throw std::invalid_argument("Unknown Merge member for " + std::string(name));',
                 "}"]
        return function

    def emit(self):
        return "\n\n".join("\n".join(body) for body in reversed(self.functions))


def r_type(port):
    if port.kind == "Path":
        return "character path"
    if port.merge:
        return " | ".join(f'list(type = "{tag}", value = <{member.type}>)'
                          for member, tag in zip(port.merge, merge_tags(port.merge)))
    return f"list <{port.type}>"


def generate(card, schemas):
    codecs = Codecs(schemas, FLATC_KEYWORDS)
    ports = Ports(codecs)
    read = []
    write = []
    for port in card.inputs:
        name = quote(port.name)
        if port.kind == "Path":
            read += [f"  if (const auto path = buffer.ReadPath({name}))",
                     f"    ports[{name}] = path->string();"]
            continue
        value = (f"{ports.decode_merge(port.type, port.merge)}(*port, {name})" if port.merge
                 else f"To{codecs.use(port.type)}(*port)")
        read += [f"  if (const auto port = buffer.ReadPort<{cpp(port.type)}>({name}))",
                 f"    ports[{name}] = {value};"]
    params = codecs.use(card.params_type)
    read += [
        "  buffer.CheckConsumed();",
        f'  const auto params = To{params}(ReadValue<{cpp(card.params_type)}, '
        f'{str(not card.params).lower()}>(buffer.Params(), "params"));',
        '  input = Json{{"ports", std::move(ports)}, {"params", params}};',
    ]
    for port in card.outputs:
        name = quote(port.name)
        required = str(port.required).lower()
        if port.kind == "Path":
            value_type = "std::filesystem::path"
            value = "FromValue<std::string>(*item)"
            store = f"WritePath(result, {name}, port, {required});"
        else:
            value_type = native(port.type)
            value = (f"{ports.encode_merge(port.type, port.merge)}(*item, {name})" if port.merge
                     else f"From{codecs.use(port.type)}(*item)")
            store = f"WritePort<{cpp(port.type)}>(result, {name}, port, {required});"
        write += [
            "  {",
            f"    const auto item = output.find({name});",
            f"    std::optional<{value_type}> port;",
            "    if (item != output.end()) {",
            f"      if (!item->is_null()) port = {value};",
            "      output.erase(item);",
            "    }",
            f"    {store}",
            "  }",
        ]
    return """// Generated by the Tsugumi R SDK.
#include <filesystem>
#include <optional>
#include <string_view>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "adapter.hh"
#include "bridge.hh"

namespace {
using namespace tsugumi::abi;
using namespace tsugumi::r;

""" + codecs.emit() + "\n\n" + ports.emit() + """

auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {
  Tsugumi::Card::CardResultT result;
  Json input;
  try {
    Input buffer(bytes);
    Json ports = Json::object();
""" + "\n".join("  " + line for line in read) + """
  } catch (const std::invalid_argument& error) {
    Reject(FailureKind::invalid_request, error.what());
  }
  auto reply = RunR(input);
  if (const auto failure = reply.find("failure"); failure != reply.end())
    return ReadFailure(*failure);
  try {
    auto& output = reply.at("output");
""" + "\n".join("  " + line for line in write) + """
    if (!output.empty())
      Reject(FailureKind::output_invalid, "Unknown output " + output.begin().key());
  } catch (const nlohmann::json::exception& error) {
    Reject(FailureKind::output_invalid, error.what());
  } catch (const std::invalid_argument& error) {
    Reject(FailureKind::output_invalid, error.what());
  }
  return result;
}

}  // namespace

extern "C" __attribute__((visibility("default")))
auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {
  return tsugumi::abi::Invoke(call, result, RunCard);
}
"""


API_NOTES = """
## R values

`main(input)` reads `input$port` (or `input[["port"]]`) for an input it
needs; an absent input raises a Card failure of kind `input_missing`.
`optional_input(input, "port")` returns `NULL` for an absent input.
Read parameters from `input$params`; use `input[["params"]]` for a port
named `params`. Return a named list of outputs.

A table is a named list keyed by the field names in the `.fbs` files in this
directory. A Path port is a character string. A Merge of mixed types is
`list(type = "Member", value = table)`.
"""


if __name__ == "__main__":
    input_api, source, out, reflection = (Path(path).resolve() for path in sys.argv[2:])
    card = load_card_schemas(source, reflection)
    schemas = [reflect_schema(source / f"_{side}.fbs", reflection)
               for side in ("inputs", "outputs", "params")]
    (out / "src").mkdir(parents=True, exist_ok=True)
    (out / "src/r_adapter.cc").write_text(generate(card, schemas), encoding="utf-8")
    api = out / "api"
    api.mkdir()
    write_api_readme(
        api / "README.md",
        ((port.name, r_type(port) + " or NULL") for port in card.inputs),
        ((port.name, r_type(port) + (" (required output)" if port.required else ""))
         for port in card.outputs),
        ((param.name, param.type) for param in card.params),
    )
    with (api / "README.md").open("a", encoding="utf-8") as readme:
        readme.write(API_NOTES)
    overview = (api / "README.md").read_text(encoding="utf-8")
    (api / "card.R").write_text(
        "\n".join(f"# {line}".rstrip() for line in overview.splitlines())
        + "\n\n" + input_api.read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    for side in ("inputs", "outputs", "params"):
        shutil.copyfile(source / f"_{side}.fbs", api / f"_{side}.fbs")
