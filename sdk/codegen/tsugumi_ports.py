"""Reflect the three catalogs prepared for each Card SDK."""

from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

from card_interface import schema_tokens


@dataclass(frozen=True)
class Candidate:
    schema: str
    type: str


@dataclass(frozen=True)
class Port:
    name: str
    type: str
    required: bool
    candidates: tuple[Candidate, ...] = ()
    collected_members: tuple[Candidate, ...] = ()


@dataclass(frozen=True)
class Param:
    name: str
    type: str


@dataclass(frozen=True)
class CardSchemas:
    inputs: tuple[Port, ...]
    outputs: tuple[Port, ...]
    params: tuple[Param, ...]


def reflect_schema(schema, reflection_fbs, *, flatc="flatc"):
    """Return the JSON representation of flatc's BFBS reflection schema."""
    schema = Path(schema).resolve()
    reflection_fbs = Path(reflection_fbs).resolve()
    with TemporaryDirectory() as directory:
        subprocess.run(
            [flatc, "-b", "--schema", "--bfbs-builtins", "-o", directory,
             "-I", str(schema.parent), str(schema)],
            check=True,
        )
        bfbs = Path(directory) / schema.with_suffix(".bfbs").name
        subprocess.run(
            [flatc, "-t", "--strict-json", "-o", directory, str(reflection_fbs),
             "--", str(bfbs)],
            check=True,
        )
        metadata = bfbs.with_suffix(".json").read_text(encoding="utf-8")
        # flatc emits these literals even with --strict-json.
        literals = {"inf": "Infinity", "-inf": "-Infinity", "nan": "NaN"}
        metadata = re.sub(
            r'(^\s*"default_real":\s*)([+-]?)(inf|nan)(?=[\s,}])',
            lambda match: match[1] + literals.get(match[2] + match[3], literals[match[3]]),
            metadata,
            flags=re.MULTILINE,
        )
        return json.loads(metadata)


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


def root_table(schema, path):
    table = schema.get("root_table")
    if table is None:
        raise ValueError(f"{path}: missing root_type")
    return table


# These shared roots cannot carry a per-candidate attribute. Their fixed ids
# are taken from core schema_id for the ABI schemas in abi/.
ABI_SCHEMA_IDS = {
    "tsugumi.Artifact": "7435657903802bed9dbd0ee94f0be71ff432073805511fc15af8acca6ac83bad",
    "tsugumi.builtin.Collected": "1c934d38885fb58ebdf2370bed0862db46f2bfa62677fe6bb801af8e4ce1d8e6",
}


def schema_id(node, where):
    value = attributes(node).get("tsugumi_schema", ABI_SCHEMA_IDS.get(node["name"], ""))
    if not re.fullmatch("[0-9a-f]{64}", value):
        raise ValueError(f"{where}: expected a tsugumi_schema attribute")
    return value


def load_ports(schema, path, *, inputs=False):
    result = []
    objects = schema["objects"]
    for field in fields(root_table(schema, path)):
        if field.get("deprecated"):
            continue
        name = field["name"]
        where = f"{path}: port {name}"
        type_ = field["type"]
        base = type_.get("base_type")
        if base == "Union" and inputs:
            union = schema["enums"][type_["index"]]
            candidates = []
            for variant in union["values"]:
                if variant.get("value", 0) == 0:
                    continue
                member = variant["union_type"]
                if member.get("base_type") != "Obj":
                    raise ValueError(f"{where}: expected table candidates")
                obj = objects[member["index"]]
                if obj.get("is_struct"):
                    raise ValueError(f"{where}: expected table candidates")
                candidates.append(Candidate(schema_id(obj, where), obj["name"]))
            if len(candidates) < 2 or len({v.schema for v in candidates}) != len(candidates):
                raise ValueError(f"{where}: expected distinct Merge candidates")
            result.append(Port(name, union["name"], True, tuple(candidates)))
        elif base == "Obj" and not objects[type_["index"]].get("is_struct"):
            obj = objects[type_["index"]]
            type_name = obj["name"]
            candidates = ()
            collected_members = ()
            if inputs:
                if type_name == "tsugumi.builtin.Collected":
                    schema_node = field if "tsugumi_schema" in attributes(field) else obj
                    candidates = (Candidate(schema_id(schema_node, where), type_name),)
                    prefix = f"inputs.{name}."
                    collected_members = tuple(
                        Candidate(schema_id(member, where), member["name"])
                        for member in objects
                        if member["name"].startswith(prefix) and "tsugumi_schema" in attributes(member)
                    )
                else:
                    candidates = (Candidate(schema_id(field if "tsugumi_schema" in attributes(field) else obj, where), type_name),)
            result.append(Port(name, type_name, field.get("required", False), candidates,
                               collected_members))
        else:
            raise ValueError(f"{where}: expected a table port")
    return tuple(result)


def load_card_schemas(schemas, reflection_fbs, *, flatc="flatc"):
    """Return typed ports and params, in declaration (field ID) order."""
    directory = Path(schemas)
    inputs, outputs = (directory / f"_{side}.fbs" for side in ("inputs", "outputs"))
    params = json.loads((directory / "param-types.json").read_text())
    return CardSchemas(
        load_ports(reflect_schema(inputs, reflection_fbs, flatc=flatc), inputs, inputs=True),
        load_ports(reflect_schema(outputs, reflection_fbs, flatc=flatc), outputs),
        tuple(Param(name, type_) for name, type_ in params.items()),
    )


def param_members(names, reserved):
    """Map arbitrary param keys to distinct portable identifiers."""
    used = set(reserved)
    for name in names:
        member = re.sub(r"[^A-Za-z0-9_]", "_", name)
        if not member[0].isalpha() or "__" in member:
            member = "param_" + re.sub(r"_+", "_", member).strip("_")
        while member in used:
            member += "_"
        used.add(member)
        yield member


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
