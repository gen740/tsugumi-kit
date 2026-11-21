"""Generate card.hh and the ABI 9 adapter from the core's _contract.json."""

import json
import re
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

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
KINDS = {"file": "File", "directory": "Directory", "flatbuffer": "FlatBuffer"}
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


def generate_headers(contract_dir, out, reflection):
    # Use the same names in all schemas and in card.hh. The rewritten schemas
    # are private to the C++ build and retain field IDs, enum values and layout.
    sources = {file.relative_to(contract_dir): file.read_text(encoding="utf-8")
               for file in contract_dir.rglob("*.fbs") if not file.name.startswith("._")}
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
        subprocess.run(["flatc", "-b", "--schema", "--bfbs-builtins", "_outputs.fbs"],
                       cwd=work, check=True)
        subprocess.run(["flatc", "-t", "--strict-json", str(reflection), "--", "_outputs.bfbs"],
                       cwd=work, check=True)
        return names, json.loads((work / "_outputs.json").read_text())


def ports(entries, reserved, names=None):
    used = (KEYWORDS if names is None else CPP_KEYWORDS) | set(reserved)
    names = names or {}
    for port in entries:
        name = port["name"]
        member = names.get(name, name) if name.startswith("_") or "__" in name else name
        while member in used:
            member += "_"
        used.add(member)
        yield (
            json.dumps(name),
            member,
            "::" + "::".join(names.get(part, part) for part in port["type"].split(".")),
            "PortKind::" + KINDS[port["kind"]],
            port["required"],
        )


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


def generate(contract, out, names=None, schema=None):
    out.joinpath("include").mkdir(parents=True, exist_ok=True)
    out.joinpath("src").mkdir(parents=True, exist_ok=True)
    inputs = list(ports(contract["inputs"], {"Inputs", "params"}, names))
    outputs = list(ports(contract["outputs"], {"Output"}, names))
    hh = [
        "// Generated by the Tsugumi ABI 9 SDK. Do not edit.",
        "#pragma once",
        "",
        "#include <optional>",
        '#include "tsugumi/abi9/card_runtime.hh"',
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        "",
        "namespace Card {",
        "",
        "using Params = ::parameters::ParamsT;",
        "struct Inputs {",
    ]
    for _, member, table, _, required in inputs:
        native = f"{table}::NativeTableType"
        hh.append(f"  {native if required else f'std::optional<{native}>'} {member};")
    hh += ["  ::Card::Params params;", "};", "", "struct Output {"]
    for _, member, table, _, required in outputs:
        hh.append(f"  std::optional<{table}::NativeTableType> {member};"
                  + ("  // Required." if required else ""))
    hh += ["};", "", "}  // namespace Card", "",
           "auto run(const Card::Inputs& input) -> Card::Output;", ""]

    cc = [
        "// Generated by the Tsugumi ABI 9 SDK. Do not edit.",
        '#include "card.hh"',
        '#include "adapter.hh"',
        "",
        "namespace {",
        "using tsugumi::abi9::Input;",
        "using tsugumi::abi9::PortKind;",
        "using tsugumi::abi9::ReadValue;",
        "using tsugumi::abi9::WritePort;",
        "",
        *(validators(schema, (table for _, _, table, _, _ in outputs)) if schema is not None else []),
        "auto RunCard(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {",
        "  Input buffer(bytes);",
        "  Card::Inputs input;",
    ]
    for name, member, table, kind, required in inputs:
        read = f"buffer.ReadPort<{table}>({name}, {kind}, {str(required).lower()})"
        cc.append(f"  input.{member} = {'*' if required else ''}{read};")
    cc += [
        "  buffer.CheckConsumed();",
        '  input.params = ReadValue<::parameters::Params>(buffer.Params(), "params");',
        "  const auto output = run(input);" if outputs else "  run(input);",
        "  Tsugumi::Card::CardResultT result;",
    ]
    validator = ", IsPackable" if schema is not None else ""
    for name, member, table, kind, required in outputs:
        cc.append(f"  WritePort<{table}>(result, {name}, {kind}, output.{member}, "
                  f"{str(required).lower()}{validator});")
    cc += [
        "  return result;",
        "}",
        "",
        "}  // namespace",
        "",
        'extern "C" __attribute__((visibility("default")))',
        "auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result) -> int {",
        "  return tsugumi::abi9::Invoke(call, result, RunCard);",
        "}",
        "",
    ]
    out.joinpath("include/card.hh").write_text("\n".join(hh))
    out.joinpath("src/card_adapter.cc").write_text("\n".join(cc))


if __name__ == "__main__":
    contract_file, out, *reflection = (Path(path).resolve() for path in sys.argv[1:])
    contract = json.loads(contract_file.read_text())
    if reflection:
        (out / "include").mkdir(parents=True, exist_ok=True)
        names, schema = generate_headers(contract_file.parent, out, reflection[0])
        generate(contract, out, names, schema)
    else:
        # Other SDKs supply their own FlatBuffers headers and wire adapters.
        generate(contract, out)
