"""Generate typed C++ JSON codecs from flatc's build-time schema descriptions."""

import importlib.util
import json
import sys
from pathlib import Path


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
        return f"ToArray({value}, [](const auto& item) {{ return {converted}; }})"

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
        return f"ReadVector({value}, [](const Json& item) {{ return {converted}; }})"

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
                to.append(f"  json[{name}] = ToArray(*{value}, [](const auto& item) {{ return {convert}; }});")
                convert = self.convert(element, schema, "From", "item")
                from_.append(f"  const auto array_{key} = ReadArray<{item_type}, {length}>({source}, [](const Json& item) {{ return static_cast<{item_type}>({convert}); }});")
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


def generate(contract, schemas, shared):
    codecs = Codecs(schemas, shared.KEYWORDS)
    read = []
    write = []
    inputs = shared.ports(contract["inputs"], {"Inputs", "params"})
    for name, _, table, kind, required in inputs:
        codec = codecs.use(table.removeprefix("::").replace("::", "."))
        read += [
            f"  if (const auto port = buffer.ReadPort<{table}>({name}, {kind}, {str(required).lower()}))",
            f"    input[{name}] = To{codec}(*port);",
        ]
    params = codecs.use("parameters.Params")
    read += ["  buffer.CheckConsumed();",
             f'  input["params"] = To{params}(ReadValue<::parameters::Params>(buffer.Params(), "params"));']
    outputs = shared.ports(contract["outputs"], {"Output"})
    for name, _, table, kind, required in outputs:
        codec = codecs.use(table.removeprefix("::").replace("::", "."))
        write += [
            "  {",
            f"    const auto item = output.find({name});",
            f"    std::optional<{table}T> port;",
            "    if (item != output.end()) {",
            f"      if (!item->is_null()) port = From{codec}(*item);",
            "      output.erase(item);",
            "    }",
            f"    WritePort<{table}>(result, {name}, {kind}, port, {str(required).lower()});",
            "  }",
        ]
    return """// Generated by the Tsugumi ABI 9 R SDK.
#include "card.hh"
#include "adapter.hh"
#include "bridge.hh"

namespace {
using namespace tsugumi::abi9;
using namespace tsugumi::r;

""" + codecs.emit() + """

auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {
  Tsugumi::Card::CardResultT result;
  Json input = Json::object();
  try {
    Input buffer(bytes);
""" + "\n".join("  " + line for line in read) + """
  } catch (const std::invalid_argument& error) {
    Reject(FailureKind::invalid_request, error.what());
  }
  auto reply = RunR(input);
  if (const auto failure = reply.find("failure"); failure != reply.end()) {
    result.failure = std::make_unique<Tsugumi::Card::CardFailureT>();
    result.failure->kind = failure->value("kind", "") == "output_invalid"
        ? FailureKind::output_invalid : FailureKind::worker_failed;
    result.failure->message = failure->at("message").get<std::string>();
    result.failure->source = failure->value("source", "");
    result.failure->line = failure->value("line", 0u);
    return result;
  }
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
  return tsugumi::abi9::Invoke(call, result, RunCard);
}
"""


if __name__ == "__main__":
    shared_path, contract_path, *schemas = map(Path, sys.argv[1:])
    spec = importlib.util.spec_from_file_location("card_cpp", shared_path)
    shared = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(shared)
    print(generate(json.loads(contract_path.read_text()),
                   [json.loads(path.read_text()) for path in schemas], shared))
