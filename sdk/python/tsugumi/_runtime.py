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
        from card import Input, Output, _inputs, _outputs, _union_checks

        run = importlib.import_module("main").run
        if not callable(run):
            raise TypeError("main.run must be callable")
        return (run, Input, Output, _inputs, _outputs, _union_checks), None
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


def _outputs_to_ports(value, output_type, outputs, union_checks):
    if not isinstance(value, output_type):
        raise Failure("output_invalid", "run must return a card.Output")
    for name, member, cls, required in outputs:
        item = getattr(value, member)
        if item is None:
            if required:
                raise Failure("output_invalid", f"Missing output {name}")
            continue
        try:
            if not isinstance(item, cls):
                raise TypeError(f"Expected {cls.__name__}")
            _check_unions(item, cls, union_checks)
            yield name, _pack(item)
        except Exception as error:
            raise Failure("output_invalid", f"Invalid output {name}: {error}") from error


def invoke(ports):
    card, failure = _load_card()
    if failure is not None:
        return failure
    run, Input, Output, inputs, outputs, union_checks = card
    values = {}
    for name, member, types in inputs:
        try:
            schema, data = ports[name]
            values[member] = types[schema].InitFromPackedBuf(data)
        except Exception as error:
            return _failure(Failure("input_invalid", f"Invalid input {name}: {error}"))
    try:
        return list(_outputs_to_ports(run(Input(**values)), Output, outputs, union_checks))
    except BaseException as error:
        return _failure(error)
