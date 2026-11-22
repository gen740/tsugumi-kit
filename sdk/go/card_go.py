"""Generate the Go Card API and its typed ABI verification and output bridge."""

import json
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, write_api_readme


def exported(name):
    result = "".join(part[:1].upper() + part[1:] for part in name.split("_"))
    return result if result and result[0].isalpha() else "Port" + result


def unique(name, used):
    while name in used:
        name += "_"
    used.add(name)
    return name


def members(ports, reserved=()):
    used = set(reserved)
    return [(port, unique(exported(port.name), used)) for port in ports]


class GoTypes:
    def __init__(self):
        self.packages = {}
        self.variants = []
        self.functions = []
        self.used = set()

    def table(self, name):
        namespace, _, leaf = name.rpartition(".")
        package = namespace.replace(".", "/")
        alias = self.packages.setdefault(package, f"port{len(self.packages)}")
        return f"{alias}.{leaf}"

    def value(self, type_, merge, alias, *, output):
        if not merge:
            self.variants.append(f"type {alias} = {self.table(type_)}T")
            return None

        used = set()
        choices = []
        for item in merge:
            field = unique(exported(item.type.rsplit(".", 1)[-1]), used)
            member_type = self.table(item.type) + "T"
            helper = None
            if item.merge:
                member_type = unique(alias + field, self.used)
                helper = self.value(item.type, item.merge, member_type, output=output)
            choices.append((item, field, member_type, helper))

        self.variants.extend([
            f"// {alias} holds exactly one Merge member.",
            f"type {alias} struct {{",
            *(f"{field} *{member_type}" for _, field, member_type, _ in choices),
            "}",
        ])
        table = self.table(type_)
        package, root = table.split(".")
        if output:
            function = "encode" + alias
            lines = [
                f"func {function}(value *{alias}) (*{table}T, error) {{",
                "var selected *" + package + ".ArrivalT",
                "count := 0",
            ]
            for item, field, _, helper in choices:
                box = self.table(item.box_type)
                lines += [f"if member := value.{field}; member != nil {{", "count++"]
                if helper:
                    lines += [f"nested, err := {helper}(member)",
                              "if err != nil { return nil, err }", "memberBytes := pack(nested)"]
                else:
                    lines += ["memberBytes := pack(member)"]
                lines += [
                    f"selected = &{package}.ArrivalT{{Type: {item.value}, Value: &{box}T{{Bytes: memberBytes}}}}",
                    "}",
                ]
            lines += [
                'if count != 1 { return nil, fmt.Errorf("expected one Merge member") }',
                f"return &{table}T{{Value: selected}}, nil",
            ]
        else:
            function = "decode" + alias
            lines = [
                f"func {function}(bytes []byte, name string) (*{alias}, error) {{",
                f"merged := {package}.GetRootAs{root}(bytes, 0)",
                "var raw flatbuffers.Table",
                "if merged.Value(&raw) {",
                "switch merged.ValueType() {",
            ]
            for item, field, _, helper in choices:
                box = self.table(item.box_type)
                child_package, child = self.table(item.type).split(".")
                lines += [f"case {item.value}:", f"var box {box}", "box.Init(raw.Bytes, raw.Pos)"]
                if helper:
                    lines += [f"value, err := {helper}(box.BytesBytes(), name)",
                              "if err != nil { return nil, err }"]
                else:
                    lines += [f"value := {child_package}.GetRootAs{child}(box.BytesBytes(), 0).UnPack()"]
                lines += [f"return &{alias}{{{field}: value}}, nil"]
            lines += ["}", "}", 'return nil, fmt.Errorf("invalid Merge input %q", name)']
        self.functions.extend([*lines, "}", ""])
        return function


def generate_go(card, inputs, outputs):
    types = GoTypes()
    types.used = {"Input" + member for _, member in inputs} | {"Output" + member for _, member in outputs}
    helpers = {}
    for side, entries in (("Input", inputs), ("Output", outputs)):
        for port, member in entries:
            alias = side + member
            if port.kind == "Path":
                types.variants.append(f"type {alias} = string")
            else:
                helpers[alias] = types.value(port.type, port.merge, alias, output=side == "Output")

    params = types.table(card.params_type)
    package, root = params.split(".")
    lines = [
        f"type Params = {params}T",
        *types.variants,
        "",
        "type Inputs struct {",
        "params *Params",
        *(f"input{member} *Input{member}" for _, member in inputs),
        "}",
        "func (input Inputs) Params() *Params { return input.params }",
    ]
    for _, member in inputs:
        lines += [
            f"func (input Inputs) {member}() *Input{member} {{",
            f"return input.input{member}",
            "}",
        ]
    lines += [
        "func (input Inputs) value(name string) any {",
        "switch name {",
        *(f"case {json.dumps(port.name)}: if value := input.input{member}; value != nil {{ return value }}"
          for port, member in inputs),
        "}",
        "return nil",
        "}",
        "type Output struct {",
        *(f"{member} *Output{member}" for _, member in outputs),
        "}",
        "",
        "func readInput(frame *wire.CardInput) (Inputs, error) {",
        "var input Inputs",
        "params := frame.ParamsBytes()",
        'if !verifyInput(0, params) { return input, fmt.Errorf("invalid params FlatBuffer") }',
        f"input.params = {package}.GetRootAs{root}(params, 0).UnPack()",
        "for i := 0; i < frame.InputsLength(); i++ {",
        "var port wire.CardPort",
        "frame.Inputs(&port, i)",
        "name := string(port.Name())",
        "switch name {",
    ]
    for index, (port, member) in enumerate(inputs, 1):
        field = f"input.input{member}"
        lines += [
            f"case {json.dumps(port.name)}:",
            f'if {field} != nil {{ return input, fmt.Errorf("duplicate input %q", name) }}',
            f'if port.Kind() != Data.PortKind{port.kind} {{ return input, fmt.Errorf("wrong kind for input %q", name) }}',
        ]
        if port.kind == "Path":
            lines += [
                "value := string(port.Path())",
                'if !filepath.IsAbs(value) || strings.ContainsRune(value, 0) || port.BytesLength() != 0 {',
                'return input, fmt.Errorf("invalid Path input %q", name)',
                "}",
                f"{field} = &value",
            ]
        else:
            lines += [
                "bytes := port.BytesBytes()",
                f'if port.Path() != nil || !verifyInput({index}, bytes) {{ return input, fmt.Errorf("invalid FlatBuffer for input %q", name) }}',
            ]
            if helper := helpers["Input" + member]:
                lines += [f"value, err := {helper}(bytes, name)",
                          "if err != nil { return input, err }", f"{field} = value"]
            else:
                package, root = types.table(port.type).split(".")
                lines += [f"{field} = {package}.GetRootAs{root}(bytes, 0).UnPack()"]
    lines += [
        'default: return input, fmt.Errorf("unknown input %q", name)',
        "}", "}", "return input, nil", "}",
        "func (output Output) send(result unsafe.Pointer) error {",
    ]
    for index, (port, member) in enumerate(outputs):
        name = json.dumps(port.name)
        lines += [f"if value := output.{member}; value != nil {{"]
        if port.kind == "Path":
            lines += [f'if !writePath(result, {index}, *value) {{ return fmt.Errorf("invalid Path output %q", {name}) }}']
        else:
            pack_value = "value"
            if helper := helpers["Output" + member]:
                lines += [f"boxed, err := {helper}(value)",
                          f'if err != nil {{ return fmt.Errorf("output %q: %w", {name}, err) }}']
                pack_value = "boxed"
            lines += [
                f'if !writeOutput(result, {index}, pack({pack_value})) {{ return fmt.Errorf("invalid FlatBuffer for output %q", {name}) }}',
            ]
        if port.required:
            lines += [f'}} else {{ return fmt.Errorf("missing output %q", {name})']
        lines.append("}")
    lines += ["return nil", "}", "", *types.functions]

    imports = ['"fmt"', '"unsafe"', 'wire "tsugumi/Tsugumi/Card"']
    if inputs:
        imports += ['"tsugumi/Tsugumi/Data"']
    if any(port.kind == "Path" for port, _ in inputs):
        imports += ['"path/filepath"', '"strings"']
    if any(port.merge for port, _ in inputs):
        imports += ['"tsugumi/flatbuffers"']
    imports += [f'{alias} "tsugumi/{path}"' for path, alias in types.packages.items()]
    return "\n".join([
        "// Code generated by the Tsugumi Go SDK. DO NOT EDIT.",
        "package tsugumi",
        "import (", *imports, ")", "",
        *lines,
    ])


def generate_cpp(card, inputs, outputs, names):
    def cpp_type(name):
        return "::" + "::".join(names.get(part, part) for part in name.split("."))

    functions = []

    def verify(type_, merge):
        table = cpp_type(type_)
        if not merge:
            return f"Buffer(bytes).Verify<{table}>() != nullptr"
        variants = []
        for item in merge:
            box = cpp_type(item.box_type)
            check = verify(item.type, item.merge)
            variants.extend([
                f"    case {item.value}: {{",
                f"      const auto* box = static_cast<const {box}*>(merged->value());",
                "      if (!box || !box->bytes()) return false;",
                "      const tsugumi_bytes bytes{box->bytes()->data(), box->bytes()->size()};",
                f"      return {check};",
                "    }",
            ])
        function = f"VerifyMerge{len(functions)}"
        functions.extend([
            f"auto {function}(tsugumi_bytes bytes) -> bool {{",
            "  const Buffer buffer(bytes);",
            f"  const auto* merged = buffer.Verify<{table}>();",
            "  if (!merged) return false;",
            "  switch (static_cast<unsigned>(merged->value_type())) {",
            *variants, "    default: return false;", "  }", "}", "",
        ])
        return f"{function}(bytes)"

    input_cases = [f"      case 0: return {verify(card.params_type, ())};"]
    output_cases = []
    for index, (port, _) in enumerate(inputs, 1):
        if port.kind == "FlatBuffer":
            input_cases.append(f"      case {index}: return {verify(port.type, port.merge)};")
    for index, (port, _) in enumerate(outputs):
        name = json.dumps(port.name)
        if port.kind == "Path":
            body = f"tsugumi::abi::WritePath(result, {name}, std::filesystem::path(std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.size)), true);"
        else:
            body = f"if (!({verify(port.type, port.merge)})) return false;\n        WritePort(result, {name}, bytes);"
        output_cases.extend([f"      case {index}:", f"        {body}", "        return true;"])

    lines = [
        "// Code generated by the Tsugumi Go SDK. DO NOT EDIT.",
        '#include "host.h"',
        '#include "adapter.hh"',
        '#include "_inputs_generated.h"',
        '#include "_outputs_generated.h"',
        '#include "_params_generated.h"',
        "",
        "namespace {",
        "using Tsugumi::Card::CardResultT;",
        "using tsugumi::abi::Buffer;",
        "",
        *functions,
    ]
    if any(port.kind == "FlatBuffer" for port, _ in outputs):
        lines += [
            "auto WritePort(CardResultT& result, const char* name, tsugumi_bytes bytes) -> void {",
            "  auto port = std::make_unique<Tsugumi::Card::CardPortT>();",
            "  port->name = name;",
            "  port->kind = Tsugumi::Data::PortKind::FlatBuffer;",
            "  port->bytes.assign(bytes.data, bytes.data + bytes.size);",
            "  result.outputs.push_back(std::move(port));",
            "}",
        ]
    lines += [
        "}  // namespace",
        "",
        "auto tsugumi_go_verify_input(size_t index, const uint8_t* data, size_t size) -> int {",
        "  try {",
        "    const tsugumi_bytes bytes{data, size};",
        "    switch (index) {",
        *input_cases, "      default: return 0;", "    }", "  } catch (...) {", "    return 0;", "  }", "}",
        "",
    ]
    if outputs:
        lines += [
            "auto tsugumi_go_output(void* raw, size_t index, const uint8_t* data, size_t size) -> int {",
            "  auto& result = *static_cast<CardResultT*>(raw);",
            "  try {",
            "    const tsugumi_bytes bytes{data, size};",
            "    switch (index) {",
            *output_cases, "      default: return 0;", "    }", "  } catch (...) {", "    return 0;", "  }", "}",
        ]
    else:
        lines += ["auto tsugumi_go_output(void*, size_t, const uint8_t*, size_t) -> int { return 0; }"]
    return "\n".join([*lines, ""])


def main(schemas, directory, reflection, cpp_directory, cpp_sdk):
    # The verification bridge uses the same C++ identifier handling as the C++ SDK.
    sys.path.insert(0, str(cpp_sdk))
    from card_cpp import generate_headers

    card = load_card_schemas(schemas, reflection)
    inputs = members(card.inputs, ("Params",))
    outputs = members(card.outputs)
    names, _ = generate_headers(schemas, cpp_directory, reflection)
    (directory / "card.go").write_text(generate_go(card, inputs, outputs))
    (directory / "card_ports.cc").write_text(generate_cpp(card, inputs, outputs, names))
    write_api_readme(
        directory / "README.md",
        ((port.name, f"*Input{member}") for port, member in inputs),
        ((port.name, f"*Output{member}" + (" (required output)" if port.required else ""))
         for port, member in outputs),
        ((param.name, param.type) for param in card.params),
    )


if __name__ == "__main__":
    main(*(Path(path).resolve() for path in sys.argv[1:]))
