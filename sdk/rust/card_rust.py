"""Generate the borrowed input and owned output types for one Card."""

import json
import re
import sys
from pathlib import Path

from tsugumi_ports import load_card_schemas, write_api_readme

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


def merge_types(port, alias, module, definitions, helpers, used, *, output):
    table = rust_type(port.type, module)
    choices = []
    variants = {"Self"}
    for item in port.merge:
        tag = item.type.rsplit(".", 1)[-1]
        variant = unique("".join(part.capitalize() for part in snake(tag).split("_")) or "Member",
                         variants)
        if variant[0].isdigit():
            variant = unique("Member" + variant, variants)
        child = rust_type(item.type, module)
        typ = f"super::{child}T" if output else f"super::{child}<'a>"
        helper = None
        if item.merge:
            typ = unique(alias + variant, used)
            helper = merge_types(item, typ, module, definitions, helpers, used, output=output)
            if not output:
                typ += "<'a>"
        choices.append((item, variant, child, typ, helper))

    lifetime = "" if output else "<'a>"
    if not output:
        definitions.append("    #[derive(Clone, Copy, Debug)]")
    definitions += [
        f"    pub enum {alias}{lifetime} {{",
        *(f"        {variant}({typ})," for _, variant, _, typ, _ in choices),
        "    }",
    ]
    enum = f"{'outputs' if output else 'inputs'}::{alias}"
    if output:
        function = f"encode_{len(helpers)}"
        body = [
            f"fn {function}(value: {enum}, name: &str) -> Result<{table}T> {{",
            f"    let mut merged = {table}T::default();",
            "    merged.value = match value {",
        ]
        union = table.rsplit("::", 1)[0] + "::ArrivalT"
        for item, variant, child, _, helper in choices:
            box = rust_type(item.box_type, module)
            body += [f"        {enum}::{variant}(value) => {{"]
            if helper:
                body.append(f"            let value = {helper}(value, name)?;")
            body += [
                f"            let mut boxed = {box}T::default();",
                f"            boxed.bytes = Some(runtime::pack_value::<{child}>(name, |builder| {{",
                "                let root = value.pack(builder);",
                "                builder.finish(root, None);",
                "            })?);",
                f"            {union}::{item.tag}(Box::new(boxed))",
                "        }",
            ]
        body += ["    };", "    Ok(merged)", "}", ""]
    else:
        function = f"decode_{len(helpers)}"
        body = [
            f"fn {function}<'a>(merged: {table}<'a>, name: &str) -> Result<{enum}<'a>> {{",
            "    match merged.value_type().0 {",
        ]
        for item, variant, child, _, helper in choices:
            accessor = flatc_snake(item.tag)
            body += [
                f"        {item.value} => {{",
                f"            let boxed = merged.value_as_{accessor}()",
                "                .ok_or_else(|| runtime::invalid_merge(name))?;",
                "            let bytes = boxed.bytes().map_or(&[][..], |bytes| bytes.bytes());",
                f"            let value = runtime::read_value::<{child}>(bytes, name)?;",
            ]
            if helper:
                body.append(f"            let value = {helper}(value, name)?;")
            body += [f"            Ok({enum}::{variant}(value))", "        }"]
        body += ["        _ => Err(runtime::invalid_merge(name)),", "    }", "}", ""]
    helpers.append(body)
    return function


def generate(card, inputs, outputs):
    helpers = []
    definitions = {}
    adapters = {}
    for entries, module, output in ((inputs, "input_types", False), (outputs, "output_types", True)):
        definitions[output] = lines = []
        used = {alias for _, _, alias in entries}
        for port, _, alias in entries:
            if port.kind == "Path":
                lines.append(f"    pub type {alias} = std::path::{'PathBuf' if output else 'Path'};")
            elif port.merge:
                adapters[output, port.name] = merge_types(
                    port, alias, module, lines, helpers, used, output=output,
                )
            else:
                typ = rust_type(port.type, module)
                suffix = "T" if output else "<'a>"
                lifetime = "" if output else "<'a>"
                lines.append(f"    pub type {alias}{lifetime} = super::{typ}{suffix};")

    params = rust_type(card.params_type, "param_types")
    lines = [
        "// Generated by the Tsugumi SDK. Do not edit.",
        '#[allow(warnings)] #[path = "card_generated.rs"] mod wire;',
        '#[allow(warnings)] #[path = "_inputs_generated.rs"] pub mod input_types;',
        '#[allow(warnings)] #[path = "_outputs_generated.rs"] pub mod output_types;',
        '#[allow(warnings)] #[path = "_params_generated.rs"] pub mod param_types;',
        "extern crate alloc;",
        "// The generated adapter selects runtime helpers according to the schemas.",
        "#[allow(dead_code)] mod runtime;",
        "mod display;",
        "pub extern crate flatbuffers;",
        "pub use display::*;",
        "pub use runtime::{Failure, FailureKind, Result, cancelled, remaining_ms, require_input};",
        "#[doc(hidden)] pub use runtime::{Bytes as __Bytes, Effects as __Effects, invoke as __invoke};",
        "",
        f"pub type Params<'a> = {params}<'a>;",
        "pub mod inputs {",
        *definitions[False],
        "}",
        "pub mod outputs {",
        *definitions[True],
        "}",
        "",
        "/// Views borrow the current call. Every input can be absent.",
        "pub struct Inputs<'a> {",
        "    pub params: Params<'a>,",
    ]
    for port, field, alias in inputs:
        typ = f"&'a std::path::Path" if port.kind == "Path" else f"inputs::{alias}<'a>"
        lines.append(f"    pub {field}: Option<{typ}>,")
    lines += ["}", "", "/// Owned output objects. Only optional ports can be omitted."]
    if not any(port.required for port, _, _ in outputs):
        lines.append("#[derive(Default)]")
    lines.append("pub struct Output {")
    for port, field, alias in outputs:
        typ = f"outputs::{alias}"
        lines.append(f"    pub {field}: {typ if port.required else f'Option<{typ}>'},")
    lines += [
        "}", "",
        "impl<'a> Inputs<'a> {",
        "    /// Look up a Path input by its wire name, including an unwired port.",
        "    pub fn path(&self, name: &str) -> Option<&'a std::path::Path> {",
        "        match name {",
        *(f"            {json.dumps(port.name)} => self.{field}," for port, field, _ in inputs
          if port.kind == "Path"),
        "            _ => None,", "        }", "    }", "",
        "    #[doc(hidden)]",
        "    pub fn __read(bytes: &'a [u8]) -> Result<Self> {",
        f"        let {'mut ' if inputs else ''}input = runtime::Input::parse(bytes)?;",
        "        Ok(Self {",
    ]
    for port, field, _ in inputs:
        name = json.dumps(port.name)
        if port.kind == "Path":
            value = f"input.take_path({name})?"
        else:
            value = f"input.take_value::<{rust_type(port.type, 'input_types')}>({name})?"
            if function := adapters.get((False, port.name)):
                value += f".map(|merged| {function}(merged, {name})).transpose()?"
        lines.append(f"            {field}: {value},")
    lines += [
        f"            params: input.params({str(not card.params).lower()})?,",
        "        })", "    }", "}", "",
        "impl runtime::PackOutput for Output {",
        "    fn pack(self, builder: &mut flatbuffers::FlatBufferBuilder<'_>) -> Result<()> {",
        f"        let {'mut ' if outputs else ''}ports = Vec::with_capacity({len(outputs)});",
    ]
    for port, field, _ in outputs:
        name = json.dumps(port.name)
        indent = "        "
        if port.required:
            lines.append(f"{indent}let value = self.{field};")
        else:
            lines.append(f"{indent}if let Some(value) = self.{field} {{")
            indent += "    "
        if port.kind == "Path":
            lines.append(f"{indent}runtime::write_path(builder, &mut ports, {name}, &value)?;")
        else:
            if function := adapters.get((True, port.name)):
                lines.append(f"{indent}let value = {function}(value, {name})?;")
            typ = rust_type(port.type, "output_types")
            lines += [
                f"{indent}runtime::write_port::<{typ}>(builder, &mut ports, {name}, |builder| {{",
                f"{indent}    let root = value.pack(builder);",
                f"{indent}    builder.finish(root, None);",
                f"{indent}}})?;",
            ]
        if not port.required:
            lines.append("        }")
    lines += [
        "        runtime::finish_outputs(builder, &ports);",
        "        Ok(())",
        "    }",
        "}",
        "",
        *(line for body in helpers for line in body),
        """/// Register a function of type `fn(Inputs<'_>) -> Result<Output>`.
#[macro_export]
macro_rules! export {
    ($run:path) => {
        #[unsafe(no_mangle)]
        unsafe extern "C" fn tsugumi_rust_invoke(
            input: $crate::__Bytes,
            host: *mut ::std::ffi::c_void,
            effects: *const $crate::__Effects,
            result: *mut $crate::__Bytes,
        ) -> ::std::ffi::c_int {
            unsafe {
                $crate::__invoke(input, host, effects, result, |bytes| {
                    let output: $crate::Output = $run($crate::Inputs::__read(bytes)?)?;
                    Ok(output)
                })
            }
        }
    };
}
""",
    ]
    return "\n".join(lines)


if __name__ == "__main__":
    schemas, directory, reflection = map(Path, sys.argv[1:])
    source_dir = directory / "src"
    for generated in source_dir.glob("*_generated.rs"):
        source = generated.read_text(encoding="utf-8")
        generated.write_text(escape_generated_rust(source), encoding="utf-8")
    card = load_card_schemas(schemas, reflection)
    inputs = list(ports(card.inputs, ("params",)))
    outputs = list(ports(card.outputs))
    (source_dir / "lib.rs").write_text(generate(card, inputs, outputs), encoding="utf-8")
    write_api_readme(
        directory / "README.md",
        ((port.name, "Option<&Path>" if port.kind == "Path" else f"Option<inputs::{alias}<'a>>")
         for port, _, alias in inputs),
        ((port.name, f"outputs::{alias} (required output)" if port.required else f"Option<outputs::{alias}>")
         for port, _, alias in outputs),
        ((param.name, param.type) for param in card.params),
    )
