"""Generate card.hh and the Card ABI adapter from the Card's FlatBuffers schemas."""

import json
import re
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from tsugumi_ports import load_card_schemas, reflect_schema, write_api_readme

KEYWORDS = set("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char
char8_t char16_t char32_t class compl concept const consteval constexpr constinit
const_cast continue co_await co_return co_yield decltype default delete do double
dynamic_cast else enum explicit export extern false float for friend goto if
inline int long mutable namespace new noexcept not not_eq nullptr operator or
or_eq private protected public register reinterpret_cast requires return short
signed sizeof static static_assert static_cast struct switch template this
thread_local throw true try typedef typeid typename union unsigned using virtual
void volatile wchar_t while xor xor_eq
""".split())

CPP_KEYWORDS = KEYWORDS | {
    "atomic_cancel", "atomic_commit", "atomic_noexcept", "contract_assert",
    "import", "module", "reflexpr", "synchronized",
}
# The pinned flatc appends an underscore for these keywords in field and
# enum value names. Preserve those names in the existing object API.
FLATC_KEYWORDS = CPP_KEYWORDS - {"char8_t", "consteval", "constinit", "contract_assert", "reflexpr"}
SCHEMAS = ("_inputs", "_outputs", "_params")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
    r'[A-Za-z_][A-Za-z_0-9]*|\d[\w.]*|[^\s]'
)


def reserved_identifier(name):
    return name in CPP_KEYWORDS or "__" in name or name.startswith("_")


def schema_tokens(source):
    return [match for match in TOKEN.finditer(source)
            if not match.group().startswith(("//", "/*"))]


def flatc_identifier(name):
    return name + "_" if name in FLATC_KEYWORDS else name


def normalize_schema(source, tokens, names):
    """Rename reserved identifiers while preserving FlatBuffers syntax."""
    pieces = []
    offset = 0
    depth = 0
    statement_start = True
    statement = ""
    in_default = False
    previous = before_previous = ""
    for i, token in enumerate(tokens):
        word = token.group()
        following = tokens[i + 1].group() if i + 1 < len(tokens) else ""
        if statement_start:
            statement = word
        replacement = word
        if word.startswith('"') and (before_previous, previous) == ("nested_flatbuffer", ":"):
            replacement = '"' + ".".join(
                names.get(part, part) for part in word[1:-1].split(".")
            ) + '"'
        elif word in names:
            declaration = statement_start and word in {"enum", "namespace", "struct", "union"}
            qualified = previous == "." or following == "."
            scalar = word in {"bool", "double", "float", "int", "long", "short"}
            builtin_type = (scalar and previous in {":", "[", "("}
                            and not qualified)
            literal = word in {"true", "false"} and previous in {"=", ":"} and not qualified
            flatc_handles_name = depth > 0 and word in FLATC_KEYWORDS and (
                following == ":"
                or (statement == "enum" and previous in {"{", ","})
                or (in_default and following != ".")
            )
            if not (declaration or builtin_type or literal or flatc_handles_name):
                replacement = names[word]
        if replacement != word:
            pieces.extend((source[offset:token.start()], replacement))
            offset = token.end()
        depth += (word == "{") - (word == "}")
        statement_start = depth == 0 and word in {";", "}"}
        if word in {",", ";", "}", "("}:
            in_default = False
        elif word == "=":
            in_default = True
        before_previous, previous = previous, word
    pieces.append(source[offset:])
    return "".join(pieces)


def generate_headers(schemas, out, reflection):
    # Use the same names in all schemas and in card.hh. The rewritten schemas
    # are private to the C++ build and retain field IDs, enum values and layout.
    sources = {file.relative_to(schemas): file.read_text(encoding="utf-8")
               for file in schemas.rglob("*.fbs") if not file.name.startswith("._")}
    tokens = {path: schema_tokens(source) for path, source in sources.items()}
    identifiers = {token.group() for matches in tokens.values() for token in matches
                   if IDENTIFIER.fullmatch(token.group())}
    names = {}
    occupied = set(identifiers)
    for name in sorted(identifiers):
        if reserved_identifier(name):
            stem = "tsugumi_" + re.sub(r"_+", "_", name).strip("_")
            stem = stem.rstrip("_")
            candidate = stem
            suffix = 2
            while any(candidate + generated in occupied
                      for generated in ("", "T", "Builder", "Union", "_type")):
                candidate = f"{stem}_{suffix}"
                suffix += 1
            names[name] = candidate
            occupied.update(candidate + generated
                            for generated in ("", "T", "Builder", "Union", "_type"))

    with TemporaryDirectory() as directory:
        work = Path(directory)
        for path, source in sources.items():
            target = work / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(normalize_schema(source, tokens[path], names))
        for schema in SCHEMAS:
            subprocess.run(["flatc", "--cpp", "--gen-object-api", "--scoped-enums",
                            "-o", str(out / "include"), f"{schema}.fbs"],
                           cwd=work, check=True)
        return names, reflect_schema(work / "_outputs.fbs", reflection)


def ports(entries, reserved, names):
    used = CPP_KEYWORDS | set(reserved)
    for port in entries:
        name = port.name
        member = names.get(name, name) if name.startswith("_") or "__" in name else name
        while member in used:
            member += "_"
        used.add(member)
        yield port, member


def cpp_type(type_, names):
    return "::" + "::".join(names.get(part, part) for part in type_.split("."))


def native_type(type_, members, names):
    if members:
        types = ", ".join(native_type(member.type, member.merge, names) for member in members)
        return f"std::variant<{types}>"
    return f"{cpp_type(type_, names)}::NativeTableType"


def port_type(port, names):
    if port.kind == "Path":
        return "std::filesystem::path"
    return native_type(port.type, port.merge, names)


def merged_leaves(type_, members):
    if not members:
        yield type_
    for member in members:
        yield from merged_leaves(member.type, member.merge)


def validators(schema, roots):
    objects, enums = schema["objects"], schema["enums"]
    tables = {obj["name"].replace(".", "::"): index for index, obj in enumerate(objects)}
    functions = {}

    def union_validator(index):
        enum = enums[index]
        tag = "::" + enum["name"].replace(".", "::")
        signature = f"auto IsPackable(const {tag}Union& choice, bool required) -> bool"
        if signature not in functions:
            functions[signature] = body = [
                "  switch (choice.type) {",
                f"    case {tag}::NONE:",
                "      return !required && !choice.value;",
            ]
            for variant in enum["values"][1:]:
                kind = variant["union_type"]
                variant_name = flatc_identifier(variant["name"])
                body.append(f"    case {tag}::{variant_name}:")
                if kind["base_type"] == "Obj" and not objects[kind["index"]].get("is_struct"):
                    table_validator(kind["index"])
                    body.append(f"      return choice.value && IsPackable(*choice.As{variant_name}());")
                else:
                    body.append("      return choice.value != nullptr;")
            body += ["  }", "  return false;"]

    def table_validator(index):
        obj = objects[index]
        table = "::" + obj["name"].replace(".", "::")
        signature = f"auto IsPackable(const {table}::NativeTableType& value) -> bool"
        if signature in functions:
            return
        functions[signature] = body = []
        for field in obj["fields"]:
            if field.get("deprecated"):
                continue
            kind = field["type"]
            base = kind["base_type"]
            member = "value." + flatc_identifier(field["name"])
            child_index = kind.get("index", -1)
            required = field.get("required", False)
            inline = any(attr["key"] == "native_inline" for attr in field.get("attributes", []))
            if base == "Obj":
                if objects[child_index].get("is_struct"):
                    if required and not inline:
                        body.append(f"  if (!{member}) return false;")
                else:
                    table_validator(child_index)
                    condition = f"!IsPackable({member})" if inline else f"!IsPackable(*{member})"
                    if not inline:
                        condition = (f"!{member} || {condition}" if required
                                     else f"{member} && {condition}")
                    body.append(f"  if ({condition}) return false;")
            elif base == "Union":
                union_validator(child_index)
                body.append(f"  if (!IsPackable({member}, {str(required).lower()})) return false;")
            elif base in {"Vector", "Vector64"}:
                element = kind["element"]
                if element == "Union":
                    union_validator(child_index)
                    check = "!IsPackable(child, true)"
                elif element == "Obj" and not objects[child_index].get("is_struct"):
                    table_validator(child_index)
                    check = "!IsPackable(child)" if inline else "!child || !IsPackable(*child)"
                else:
                    continue
                body += [f"  for (const auto& child : {member}) {{",
                         f"    if ({check}) return false;", "  }"]
        body.append("  return true;")
        if len(body) == 1:
            body.insert(0, "  (void)value;")

    for table in roots:
        table_validator(tables[table.removeprefix("::")])
    return ([signature + ";" for signature in functions] + [""]
            + [line for signature, body in functions.items()
               for line in [signature + " {", *body, "}", ""]])


def decode_merge(type_, members, names, functions):
    table = cpp_type(type_, names)
    function = f"DecodeMerge{len(functions)}"
    functions[function] = body = [
        f"auto {function}(const {table}::NativeTableType& merged, std::string_view name)",
        f"    -> {native_type(type_, members, names)} {{",
    ]
    for member in members:
        tag = flatc_identifier(names.get(member.tag, member.tag))
        target = cpp_type(member.type, names)
        read = f"ReadValue<{target}>({{box->bytes.data(), box->bytes.size()}}, name)"
        if member.merge:
            child = decode_merge(member.type, member.merge, names, functions)
            read = f"{child}({read}, name)"
        body += [f"  if (const auto* box = merged.value.As{tag}())",
                 f"    return {read};"]
    body += ['  Reject(FailureKind::invalid_request, "Invalid Merge input " + std::string(name));',
             "}", ""]
    return function


def encode_merge(type_, members, names, functions):
    table = cpp_type(type_, names)
    function = f"EncodeMerge{len(functions)}"
    functions[function] = body = [
        f"auto {function}(const {native_type(type_, members, names)}& value, std::string_view name)",
        f"    -> {table}::NativeTableType {{",
        f"  {table}::NativeTableType merged;",
    ]
    for member in members:
        box = cpp_type(member.box_type, names)
        target = cpp_type(member.type, names)
        native = native_type(member.type, member.merge, names)
        if member.merge:
            child = encode_merge(member.type, member.merge, names, functions)
            pack = f"PackValue<{target}>({child}(*item, name), name)"
        else:
            pack = f"PackValue<{target}>(*item, name, IsPackable)"
        body += [f"  if (const auto* item = std::get_if<{native}>(&value)) {{",
                 f"    {box}::NativeTableType box;",
                 f"    box.bytes = {pack};",
                 "    merged.value.Set(std::move(box));",
                 "    return merged;",
                 "  }"]
    body += ['  Reject(FailureKind::output_invalid, "Invalid Merge output " + std::string(name));',
             "}", ""]
    return function


def generate(card, out, names, output_schema):
    out.joinpath("src").mkdir(parents=True, exist_ok=True)
    out.joinpath("include").mkdir(parents=True, exist_ok=True)
    inputs = list(ports(card.inputs, {"Inputs", "get", "params"}, names))
    outputs = list(ports(card.outputs, {"Output"}, names))
    params = cpp_type(card.params_type, names)
    hh = [
        "// Generated by the Tsugumi SDK. Do not edit.",
        "#pragma once",
        "",
        "#include <filesystem>",
        "#include <optional>",
        "#include <string_view>",
        "#include <type_traits>",
        "#include <variant>",
        '#include "tsugumi/abi/card_runtime.hh"',
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        "",
        "namespace Card {",
        "",
        f"using Params = {params}::NativeTableType;",
        "namespace input_types {",
    ]
    for port, member in inputs:
        hh.append(f"using {member} = {port_type(port, names)};")
    hh += [
        "}  // namespace input_types",
        "",
        "template <tsugumi::abi::detail::InputName Name, class Fallback>",
        "using InputType = typename decltype([] {",
    ]
    if inputs:
        for index, (port, member) in enumerate(inputs):
            branch = "if" if index == 0 else "} else if"
            hh += [f"  {branch} constexpr (std::string_view(Name.text) == {json.dumps(port.name)}) {{",
                   f"    return std::type_identity<input_types::{member}>{{}};"]
        hh += ["  } else {", "    return std::type_identity<Fallback>{};", "  }"]
    else:
        hh.append("  return std::type_identity<Fallback>{};")
    hh += ["}())::type;", "", "struct Inputs {"]
    for port, member in inputs:
        hh.append(f"  std::optional<input_types::{member}> {member};")
    hh += [
        "  ::Card::Params params;",
        "",
        "  template <class T>",
        "  auto get(std::string_view name) const -> T {",
    ]
    for port, member in inputs:
        hh.append(f"    if (name == {json.dumps(port.name)})"
                  f" return tsugumi::abi::detail::GetInput<T>(name, this->{member});")
    hh += ["    return tsugumi::abi::detail::MissingInput<T>(name);",
           "  }", "};", "", "struct Output {"]
    for port, member in outputs:
        hh.append(f"  std::optional<{port_type(port, names)}> {member};"
                  + ("  // Required." if port.required else ""))
    hh += ["};", "", "}  // namespace Card", "",
           "auto run(const Card::Inputs& input) -> Card::Output;", ""]

    roots = (cpp_type(type_, names) for port, _ in outputs if port.kind == "FlatBuffer"
             for type_ in merged_leaves(port.type, port.merge))
    functions = {}
    body = [
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  Input buffer(bytes);",
        "  Card::Inputs input;",
    ]
    for port, member in inputs:
        name = json.dumps(port.name)
        if port.kind == "Path":
            body.append(f"  input.{member} = buffer.ReadPath({name});")
            continue
        table = cpp_type(port.type, names)
        read = f"buffer.ReadPort<{table}>({name})"
        if port.merge:
            decode = decode_merge(port.type, port.merge, names, functions)
            body += [f"  if (const auto merged = {read})",
                     f"    input.{member} = {decode}(*merged, {name});"]
        else:
            body.append(f"  input.{member} = {read};")
    body += [
        "  buffer.CheckConsumed();",
        f'  input.params = ReadValue<{params}>(buffer.Params(), "params");',
        "  const auto output = run(input);" if outputs else "  run(input);",
        "  Tsugumi::Card::CardResultT result;",
    ]
    for port, member in outputs:
        name = json.dumps(port.name)
        required = str(port.required).lower()
        if port.kind == "Path":
            body.append(f"  WritePath(result, {name}, output.{member}, {required});")
            continue
        table = cpp_type(port.type, names)
        value = f"output.{member}"
        validator = ", IsPackable"
        if port.merge:
            encode = encode_merge(port.type, port.merge, names, functions)
            body += [f"  std::optional<{table}::NativeTableType> {member}_boxed;",
                     f"  if ({value}) {member}_boxed = {encode}(*{value}, {name});"]
            value = f"{member}_boxed"
            validator = ""
        body.append(f"  WritePort<{table}>(result, {name}, {value}, {required}{validator});")
    body += ["  return result;", "}"]
    cc = [
        "// Generated by the Tsugumi SDK. Do not edit.",
        '#include "card.hh"',
        '#include "adapter.hh"',
        "",
        "namespace {",
        "using tsugumi::abi::FailureKind;",
        "using tsugumi::abi::Input;",
        "using tsugumi::abi::PackValue;",
        "using tsugumi::abi::ReadValue;",
        "using tsugumi::abi::Reject;",
        "using tsugumi::abi::WritePath;",
        "using tsugumi::abi::WritePort;",
        "",
        *validators(output_schema, roots),
        *(line for function in reversed(functions.values()) for line in function),
        *body,
        "",
        "}  // namespace",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi::Invoke(call, result, RunCard);",
        "}",
        "",
    ]
    out.joinpath("include/card.hh").write_text("\n".join(hh), encoding="utf-8")
    out.joinpath("src/card_adapter.cc").write_text("\n".join(cc), encoding="utf-8")
    write_api_readme(
        out / "README.md",
        ((port.name, f"std::optional<{port_type(port, names)}>") for port, _ in inputs),
        ((port.name, f"std::optional<{port_type(port, names)}>"
          + (" (required output)" if port.required else "")) for port, _ in outputs),
        ((param.name, param.type) for param in card.params),
    )


if __name__ == "__main__":
    schemas, out, reflection = (Path(path).resolve() for path in sys.argv[1:])
    card = load_card_schemas(schemas, reflection)
    (out / "include").mkdir(parents=True, exist_ok=True)
    names, output_schema = generate_headers(schemas, out, reflection)
    generate(card, out, names, output_schema)
