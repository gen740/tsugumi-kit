"""Generate the borrowed input and owned output types for one Card."""

import json
import re
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, param_members, reflect_schema, write_api_readme

KEYWORDS = set("""
as async await break const continue crate dyn else enum extern false fn for if impl in let
loop match mod move mut pub ref return self Self static struct super trait true type unsafe
use where while abstract become box do final macro override priv typeof unsized virtual
yield try gen union raw safe
""".split())

# The flatc Rust generator escapes these spellings before converting namespaces.
UNESCAPED_KEYWORDS = {"async", "await", "dyn", "gen", "try"}
FLATC_KEYWORDS = (KEYWORDS - UNESCAPED_KEYWORDS - {"raw", "safe", "union"}) | set("""
alignof offsetof proc pure sizeof std usize isize u8 i8 u16 i16 u32 i32 u64 i64
u128 i128 f32 f64 follow push to_little_endian from_little_endian ENUM_MAX
ENUM_MIN ENUM_VALUES
""".split())

def escape_generated_rust(source):
    # flatc predates these keywords and emits them as bare identifiers. Keep
    # the wire names in string literals and the generated comments intact.
    return re.sub(
        r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\b[A-Za-z_]\w*\b',
        lambda token: raw_identifier(token[0]),
        source,
    )


def raw_identifier(name):
    return f"r#{name}" if name in UNESCAPED_KEYWORDS else name


def snake(name):
    name = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()


def member(name):
    name = snake(name)
    return name + "_" if name in KEYWORDS or name == "_" else name


def flatc_snake(name):
    # Match flatc's CamelToSnake conversion, including digits.
    return re.sub(r"(?<=[a-z])(?=[A-Z0-9])|(?<=[0-9])(?=[A-Z])", "_", name).lower()


def rust_type(name, module):
    *namespace, leaf = (
        part + "_" if part in FLATC_KEYWORDS else part for part in name.split(".")
    )
    return "::".join([module, *(raw_identifier(flatc_snake(part)) for part in namespace),
                      raw_identifier(leaf)])


def unique(name, used):
    while name in used:
        name += "_"
    used.add(name)
    return name


def ports(entries, reserved=()):
    members = set(reserved)
    aliases = set(KEYWORDS)
    for port in entries:
        name = port.name
        field = unique(member(name), members)
        alias = "".join(part[:1].upper() + part[1:] for part in field.split("_"))
        if not alias:
            alias = "Port"
        elif alias[0].isdigit():
            alias = f"Port{alias}"
        yield port, field, unique(alias, aliases)


def rust_string(value):
    return '"' + "".join(
        f"\\u{{{ord(char):x}}}" if ord(char) < 32 or ord(char) == 127
        else "\\" + char if char in {'"', "\\"} else char
        for char in value
    ) + '"'


def rust_param_type(type_):
    if type_.startswith("["):
        return "&'static [" + rust_param_type(type_[1:-1]) + "]"
    return {"bool": "bool", "long": "i64", "double": "f64", "string": "&'static str"}[type_]


def rust_literal(value, type_):
    if type_.startswith("["):
        return "&[" + ", ".join(rust_literal(v, type_[1:-1]) for v in value) + "]"
    if type_ == "string":
        return rust_string(value)
    if type_ == "double":
        return repr(float(value))
    return json.dumps(value)


def param_fields(card):
    names = (member(p.name.replace("-", "_").replace(".", "_")) for p in card.params)
    names = ("param" + n if not n[0].isalpha() else n for n in names)
    return list(zip(card.params, param_members(names, KEYWORDS)))


def params_source(card, values):
    fields = param_fields(card)
    lines = ["pub struct Params {",
             *(f"    pub {name}: {rust_param_type(param.type)}," for param, name in fields),
             "}", "pub const PARAMS: Params = Params {"]
    for param, name in fields:
        if values is None:
            default = [] if param.type.startswith("[") else {
                "bool": False, "long": 0, "double": 0.0, "string": "",
            }[param.type]
            value = default
        else:
            value = values[param.name]
        lines.append(f"    {name}: {rust_literal(value, param.type)},")
    return "\n".join([*lines, "};", ""])


def generate(inputs, outputs, output_schema):
    aliases = []
    reads = []
    for port, field, alias in inputs:
        name = rust_string(port.name)
        if len(port.candidates) == 1:
            candidate, = port.candidates
            typ = rust_type(candidate.type, "input_types")
            aliases += [f"    pub type {alias}<'a> = super::{typ}<'a>;"]
            reads += [f"        let {field} = input.read({name}, {rust_string(candidate.schema)})?;"]
        else:
            aliases += ["    #[derive(Clone, Copy, Debug)]", f"    pub enum {alias}<'a> {{"]
            reads += [f"        let (schema, bytes) = input.take({name})?;",
                      f"        let {field} = match schema {{"]
            used = set()
            for candidate in port.candidates:
                typ = rust_type(candidate.type, "input_types")
                tag = candidate.type.rsplit(".", 1)[-1]
                variant = unique("".join(x.capitalize() for x in snake(tag).split("_")), used)
                aliases += [f"        {variant}(super::{typ}<'a>),"]
                reads += [f"            Some({rust_string(candidate.schema)}) => inputs::{alias}::{variant}(runtime::read_value(bytes, {name})?),"]
            aliases += ["    }"]
            reads += [f"            _ => return Err(runtime::unknown_schema({name})),", "        };"]
    lifetime = "<'a>" if inputs else ""
    borrow = "&'a " if inputs else "&"
    lines = [
        "// Generated by the Tsugumi SDK. Do not edit.",
        '#[allow(warnings)] #[path = "card_generated.rs"] mod wire;',
        '#[allow(warnings)] #[path = "_inputs_generated.rs"] pub mod input_types;',
        '#[allow(warnings)] #[path = "_outputs_generated.rs"] pub mod output_types;',
        "extern crate alloc;",
        "#[allow(dead_code)] mod runtime;",
        "mod display;",
        "mod params;",
        "pub extern crate flatbuffers;",
        "pub use display::*;",
        "pub use params::{Params, PARAMS};",
        "pub use wire::tsugumi::ArtifactT as Artifact;",
        "pub use runtime::{Failure, FailureKind, Result, cancelled, remaining_ms, require_input};",
        "#[doc(hidden)] pub use runtime::{Bytes as __Bytes, Effects as __Effects, RawInput as __RawInput, invoke as __invoke};",
        "",
        "pub mod inputs {", *aliases, "}",
        "pub mod outputs {",
        *(f"    pub type {alias} = super::{rust_type(port.type, 'output_types')}T;" for port, _, alias in outputs),
        "}",
        "",
        f"pub struct Inputs{lifetime} {{",
        *(f"    pub {field}: inputs::{alias}<'a>," for _, field, alias in inputs),
        "}",
        "",
        "#[derive(Default)]",
        "pub struct Output {",
        *(f"    pub {field}: Option<outputs::{alias}>," for _, field, alias in outputs),
        "}", "",
        f"impl{lifetime} Inputs{lifetime} {{",
        "    #[doc(hidden)]",
        f"    pub fn __read(ports: {borrow}[__RawInput]) -> Result<Self> {{",
        f"        let {'mut ' if inputs else ''}input = runtime::Input::parse(ports)?;",
        *reads,
        "        input.check_consumed()?;",
        f"        Ok(Self {{ {', '.join(field for _, field, _ in inputs)} }})",
        "    }", "}", "",
        "impl runtime::PackOutput for Output {",
        "    fn pack(self, builder: &mut flatbuffers::FlatBufferBuilder<'_>) -> Result<()> {",
        f"        let {'mut ' if outputs else ''}ports = Vec::with_capacity({len(outputs)});",
    ]
    for port, field, _ in outputs:
        name = rust_string(port.name)
        typ = rust_type(port.type, "output_types")
        lines += [
            f"        if let Some(value) = self.{field} {{",
            f"            runtime::write_port::<{typ}>(builder, &mut ports, {name}, |builder| {{",
            "                let root = value.pack(builder);",
            "                builder.finish(root, None);",
            "            })?;",
        ]
        if port.required:
            lines += ["        } else {", f"            return Err(runtime::invalid_output({name}));"]
        lines += ["        }"]
    lines += [
        "        runtime::finish_outputs(builder, &ports);",
        "        Ok(())",
        "    }",
        "}",
        "",
        """/// Register a function of type `fn(Inputs<'_>) -> Result<Output>`.
#[macro_export]
macro_rules! export {
    ($run:path) => {
        #[unsafe(no_mangle)]
        unsafe extern "C" fn tsugumi_rust_invoke(
            inputs: *const $crate::__RawInput,
            input_count: usize,
            host: *mut ::std::ffi::c_void,
            effects: *const $crate::__Effects,
            result: *mut $crate::__Bytes,
        ) -> ::std::ffi::c_int {
            unsafe {
                $crate::__invoke(inputs, input_count, host, effects, result, |ports| {
                    let output: $crate::Output = $run($crate::Inputs::__read(ports)?)?;
                    Ok(output)
                })
            }
        }
    };
}
""",
    ]
    return "\n".join(lines)


def merge_namespace_blocks(source):
    # flatc reopens a module when separate declarations share its namespace.
    # Rust requires a single module declaration at each qualified path.
    module_open = re.compile(r"pub mod (\w+) \{")
    module_close = re.compile(r"}  // pub mod \w+")
    root = ([], {})
    stack = [root]
    for line in source.splitlines():
        if line == "#[allow(unused_imports, dead_code)]":
            continue
        if match := module_open.fullmatch(line):
            child = stack[-1][1].setdefault(match[1], ([], {}))
            stack.append(child)
        elif module_close.fullmatch(line):
            stack.pop()
        else:
            stack[-1][0].append(line)
    if stack != [root]:
        raise ValueError("Unexpected namespace structure from flatc")

    def render(module):
        body, children = module
        yield from body
        for name, child in children.items():
            yield "#[allow(unused_imports, dead_code)]"
            yield f"pub mod {name} {{"
            yield from render(child)
            yield "}"

    return "\n".join(render(root)) + "\n"


if __name__ == "__main__":
    schemas, directory, reflection = map(Path, sys.argv[1:4])
    source_dir = directory / "src"
    card = load_card_schemas(schemas, reflection)
    values = json.loads(Path(sys.argv[4]).read_text()) if len(sys.argv) > 4 else None
    (source_dir / "params.rs").write_text(params_source(card, values), encoding="utf-8")
    for generated in source_dir.glob("*_generated.rs"):
        source = generated.read_text(encoding="utf-8")
        if generated.name != "card_generated.rs":
            source = re.sub(
                r"(?ms)^// struct ArtifactId,.*?^impl ArtifactT \{.*?^\}",
                "pub use crate::wire::tsugumi::{Artifact, ArtifactArgs, ArtifactBuilder, ArtifactId, ArtifactIdT, ArtifactOffset, ArtifactT};",
                source,
            )
        generated.write_text(escape_generated_rust(merge_namespace_blocks(source)), encoding="utf-8")
    inputs = list(ports(card.inputs))
    outputs = list(ports(card.outputs))
    schema = reflect_schema(schemas / "_outputs.fbs", reflection)
    (source_dir / "lib.rs").write_text(generate(inputs, outputs, schema), encoding="utf-8")
    write_api_readme(
        directory / "README.md",
        ((port.name, f"inputs::{alias}<'a>") for port, _, alias in inputs),
        ((port.name, f"Option<outputs::{alias}>" + (" (required output)" if port.required else ""))
         for port, _, alias in outputs),
        ((param.name, rust_param_type(param.type)) for param in card.params),
    )
