"""Reflect the three Card catalogs supplied to an SDK by Tsugumi."""

from dataclasses import dataclass
import json
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


@dataclass(frozen=True)
class MergeMember:
    tag: str
    value: int
    box_type: str
    type: str
    union_type: str
    merge: "tuple[MergeMember, ...]" = ()


@dataclass(frozen=True)
class Port:
    name: str
    type: str
    kind: str
    required: bool
    merge: tuple[MergeMember, ...] = ()


@dataclass(frozen=True)
class Param:
    name: str
    type: str
    required: bool
    # The field's metadata in flatc's reflection.fbs JSON format.
    field: dict


@dataclass(frozen=True)
class CardSchemas:
    inputs: tuple[Port, ...]
    outputs: tuple[Port, ...]
    params: tuple[Param, ...]
    params_type: str


def reflect_schema(schema, reflection_fbs, *, flatc="flatc"):
    """Return the JSON representation of flatc's BFBS reflection schema."""
    schema = Path(schema).resolve()
    reflection_fbs = Path(reflection_fbs).resolve()
    with TemporaryDirectory() as directory:
        subprocess.run(
            [flatc, "-b", "--schema", "--bfbs-builtins", "-o", directory,
             str(schema)],
            check=True,
        )
        bfbs = Path(directory) / schema.with_suffix(".bfbs").name
        subprocess.run(
            [flatc, "-t", "--strict-json", "-o", directory, str(reflection_fbs),
             "--", str(bfbs)],
            check=True,
        )
        return json.loads(bfbs.with_suffix(".json").read_text(encoding="utf-8"))


def attributes(field):
    return {attr["key"]: attr["value"] for attr in field.get("attributes", [])}


def field_type(schema, field):
    type_ = field["type"]
    base = type_.get("base_type", "None")
    vector = base in {"Vector", "Vector64", "Array"}
    element = type_.get("element", "None") if vector else base
    index = type_.get("index", -1)
    if index >= 0:
        table = "objects" if element == "Obj" else "enums"
        name = schema[table][index]["name"]
    else:
        name = element.lower()
    if base == "Array":
        return f"[{name}:{type_['fixed_length']}]"
    return f"[{name}]" if vector else name


def fields(table):
    return sorted(
        (field for field in table.get("fields", [])
         if field["type"].get("base_type") != "UType"),
        key=lambda field: field.get("id", 0),
    )


def merge_members(schema, root, *, nested=False):
    # Catalogs place each port's closure under a separate namespace.
    name = root["name"]
    suffix = "nested.Merged" if nested else "tsugumi.merge.Merged"
    if name != suffix and not name.endswith("." + suffix):
        return ()
    root_fields = fields(root)
    if (len(root_fields) != 1 or root_fields[0]["name"] != "value"
            or root_fields[0]["type"].get("base_type") != "Union"):
        raise ValueError(f"{name}: expected the core Merge union in value")
    union = schema["enums"][root_fields[0]["type"]["index"]]
    objects = schema["objects"]
    by_name = {obj["name"]: obj for obj in objects}
    members = []
    for variant in sorted(union["values"], key=lambda entry: entry.get("value", 0)):
        value = variant.get("value", 0)
        if value == 0:
            continue
        type_ = variant["union_type"]
        if type_.get("base_type") != "Obj":
            raise ValueError(f"{name}: expected a table for Merge member {variant['name']}")
        box = objects[type_["index"]]
        box_fields = fields(box)
        if (len(box_fields) != 1 or box_fields[0]["name"] != "bytes"
                or field_type(schema, box_fields[0]) != "[ubyte]"):
            raise ValueError(f"{box['name']}: expected a Merge box with bytes:[ubyte]")
        target = attributes(box_fields[0]).get("nested_flatbuffer")
        if target is None:
            raise ValueError(f"{box['name']}: missing nested_flatbuffer")
        namespace = box["name"].split(".")[:-1]
        # Resolve nested_flatbuffer the same way as flatc, from the box's
        # namespace toward the global namespace.
        for depth in range(len(namespace), -1, -1):
            qualified = ".".join([*namespace[:depth], target])
            if qualified in by_name:
                break
        else:
            raise ValueError(f"{box['name']}: unknown nested_flatbuffer type {target}")
        child = by_name[qualified]
        if child.get("is_struct", False):
            raise ValueError(f"{box['name']}: nested_flatbuffer must be a table")
        members.append(MergeMember(
            variant["name"], value, box["name"], qualified, union["name"],
            merge_members(schema, child, nested=True),
        ))
    if not members:
        raise ValueError(f"{name}: empty Merge union")
    return tuple(members)


def root_table(schema, path):
    table = schema.get("root_table")
    if table is None:
        raise ValueError(f"{path}: missing root_type")
    return table


def load_ports(schema, path):
    result = []
    for field in fields(root_table(schema, path)):
        name = field["name"]
        type_ = field["type"]
        required = field.get("required", False)
        marker = attributes(field).get("tsugumi")
        if marker == "path" and type_.get("base_type") == "String":
            result.append(Port(name, "string", "Path", required))
        elif marker is None and type_.get("base_type") == "Obj":
            obj = schema["objects"][type_["index"]]
            if obj.get("is_struct", False):
                raise ValueError(f"{path}: port {name} requires a table type")
            result.append(Port(name, obj["name"], "FlatBuffer", required, merge_members(schema, obj)))
        else:
            raise ValueError(f"{path}: port {name} requires a table or string (tsugumi: \"path\")")
    return tuple(result)


def load_card_schemas(schemas, reflection_fbs, *, flatc="flatc"):
    """Return typed ports and params, in declaration (field ID) order."""
    inputs, outputs, params = (Path(schemas) / f"_{side}.fbs"
                               for side in ("inputs", "outputs", "params"))
    input_schema, output_schema, param_schema = (
        reflect_schema(path, reflection_fbs, flatc=flatc)
        for path in (inputs, outputs, params)
    )
    param_root = root_table(param_schema, params)
    return CardSchemas(
        load_ports(input_schema, inputs),
        load_ports(output_schema, outputs),
        tuple(Param(field["name"], field_type(param_schema, field),
                    field.get("required", False), field)
              for field in fields(param_root)),
        param_root["name"],
    )


def write_api_readme(path, inputs, outputs, params):
    """Write a README from (wire name, language type) pairs for each section."""
    sections = ["# Card API", ""]
    for title, entries in (("Inputs", inputs), ("Outputs", outputs), ("Params", params)):
        sections.extend((f"## {title}", ""))
        entries = list(entries)
        sections.extend(
            (f"- `{name}` — `{type_}`" for name, type_ in entries) if entries else ["None."]
        )
        sections.append("")
    Path(path).write_text("\n".join(sections), encoding="utf-8")
