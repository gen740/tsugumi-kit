"""Private CPython entry point used by the native adapter."""

import importlib
import os
import sys
import traceback
from functools import cache
from pathlib import Path, PurePosixPath

import flatbuffers
from Tsugumi.Card.CardFailure import CardFailureT
from Tsugumi.Data.FailureKind import FailureKind
from Tsugumi.Data.PortKind import PortKind

from . import Failure

_SOURCE_ROOT = Path(sys.path[1])


def _pack(value):
    builder = flatbuffers.Builder()
    builder.Finish(value.Pack(builder))
    return bytes(builder.Output())


def _source_location(error):
    if isinstance(error, SyntaxError) and error.filename:
        frames = [(error.filename, error.lineno or 0)]
    else:
        frames = [(frame.f_code.co_filename, line)
                  for frame, line in traceback.walk_tb(error.__traceback__)]
    for filename, line in reversed(frames):
        path = Path(filename)
        if path.is_relative_to(_SOURCE_ROOT):
            return str(PurePosixPath("src", path.relative_to(_SOURCE_ROOT))), line
    return None, 0


def _failure(error):
    failure = CardFailureT()
    failure.kind = FailureKind.worker_failed
    try:
        message = str(error)
    except BaseException:
        message = "Cannot format exception"
    failure.message = f"{type(error).__name__}: {message}"
    failure.source, failure.line = _source_location(error)
    if isinstance(error, Failure):
        if isinstance(error.kind, str):
            kind = getattr(FailureKind, error.kind, None)
            if type(kind) is int and kind != FailureKind.unspecified:
                failure.kind = kind
        failure.message = message
        if isinstance(error.source, str):
            path = PurePosixPath(error.source)
            if path.parts and not path.is_absolute() and ".." not in path.parts:
                failure.source = str(path)
                failure.line = error.line if type(error.line) is int and 0 < error.line < 2**32 else 0
        if isinstance(error.cell, str):
            failure.cell = error.cell
    for attribute in ("message", "source", "cell"):
        value = getattr(failure, attribute)
        if value is not None:
            setattr(failure, attribute, value.encode("utf-8", errors="backslashreplace"))
    return _pack(failure)


@cache
def _load_card():
    # Cache failures as well, so module-level code is attempted once.
    try:
        os.environ["MPLCONFIGDIR"] = str(Path.home() / ".matplotlib")
        from card import Input, Output, ParamsT, _inputs, _outputs, _union_checks

        run = importlib.import_module("main").run
        if not callable(run):
            raise TypeError("main.run must be callable")
        return (run, Input, Output, ParamsT, _inputs, _outputs, _union_checks), None
    except BaseException as error:
        return None, _failure(error)


def _check_unions(value, cls, checks):
    fields = checks.get(cls)
    if fields is None:
        return
    unions, children = fields
    for member, tag_member, variants in unions:
        tag = getattr(value, tag_member)
        item = getattr(value, member)
        if tag == 0 and item is None:
            continue
        variant = variants.get(tag)
        if variant is None or not isinstance(item, variant):
            raise ValueError(f"{cls.__name__}.{member} does not match its union tag")
        _check_unions(item, type(item), checks)
    for member, child, vector in children:
        item = getattr(value, member)
        if item is None:
            continue
        if vector:
            for element in item:
                _check_unions(element, child, checks)
        else:
            _check_unions(item, child, checks)


def _unpack_port(data, cls, cases):
    if cls is None:
        return os.fsdecode(data)
    item = cls.InitFromPackedBuf(data)
    if not cases:
        return item
    case = cases.get(item.valueType)
    if case is None or not isinstance(item.value, case[0]) or item.value.bytes is None:
        raise ValueError("Invalid boxed Merge value")
    _, member, nested = case
    # The native verifier checks nested_flatbuffer fields before Python decodes them.
    return _unpack_port(bytes(item.value.bytes), member, nested)


def _pack_port(item, cls, cases, checks):
    if cases:
        encoded = _pack_merge(item, cls, cases, checks)
        if encoded is None:
            raise TypeError("Expected a member of the Merge union")
        return encoded
    if not isinstance(item, cls):
        raise TypeError(f"Expected {cls.__name__}")
    _check_unions(item, cls, checks)
    return _pack(item)


def _pack_merge(item, cls, cases, checks):
    for tag, (box, member, nested) in cases.items():
        if nested:
            payload = _pack_merge(item, member, nested, checks)
        elif isinstance(item, member):
            payload = _pack_port(item, member, nested, checks)
        else:
            continue
        if payload is not None:
            return _pack(cls(valueType=tag, value=box(bytes=payload)))
    return None


def _outputs_to_ports(value, output_type, outputs, union_checks):
    if not isinstance(value, output_type):
        raise Failure("output_invalid", "run must return a card.Output")
    for name, member, cls, cases, required in outputs:
        item = getattr(value, member)
        if item is None:
            if required:
                raise Failure("output_invalid", f"Missing output {name}")
            continue
        try:
            if cls is None:
                yield name, PortKind.Path, os.fsencode(item)
            else:
                yield name, PortKind.FlatBuffer, _pack_port(item, cls, cases, union_checks)
        except Exception as error:
            raise Failure("output_invalid", f"Invalid output {name}: {error}") from error


def invoke(params, ports):
    card, failure = _load_card()
    if failure is not None:
        return failure
    run, Input, Output, Params, inputs, outputs, union_checks = card
    values = {}
    for name, member, cls, cases, _ in inputs:
        if name not in ports:
            continue
        try:
            values[member] = _unpack_port(ports[name], cls, cases)
        except Exception as error:
            return _failure(Failure("invalid_request", f"Invalid input {name}: {error}"))
    try:
        input = Input(params=Params.InitFromPackedBuf(params) if params else Params(), **values)
        return list(_outputs_to_ports(run(input), Output, outputs, union_checks))
    except BaseException as error:
        return _failure(error)
