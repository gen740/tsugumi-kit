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
        from card import Input, Output, ParamsT, _inputs, _outputs

        run = importlib.import_module("main").run
        if not callable(run):
            raise TypeError("main.run must be callable")
        return (run, Input, Output, ParamsT, _inputs, _outputs), None
    except BaseException as error:
        return None, _failure(error)


def _outputs_to_ports(value, output_type, outputs):
    if not isinstance(value, output_type):
        raise Failure("output_invalid", "run must return a card.Output")
    for name, member, cls, kind, required in outputs:
        item = getattr(value, member)
        if item is None:
            if required:
                raise Failure("output_invalid", f"Missing output {name}")
            continue
        if not isinstance(item, cls):
            raise Failure("output_invalid", f"Output {name} must be a {cls.__name__}")
        try:
            encoded = _pack(item)
        except Exception as error:
            raise Failure("output_invalid", f"Invalid output {name}: {error}") from error
        yield name, kind, encoded


def invoke(params, ports):
    card, failure = _load_card()
    if failure is not None:
        return failure
    run, Input, Output, Params, inputs, outputs = card
    try:
        values = {
            member: cls.InitFromPackedBuf(ports[name]) if name in ports else None
            for name, member, cls, _, _ in inputs
        }
    except Exception as error:
        return _failure(Failure("invalid_request", f"Cannot unpack inputs: {error}"))
    try:
        input = Input(params=Params.InitFromPackedBuf(params), **values)
        return list(_outputs_to_ports(run(input), Output, outputs))
    except BaseException as error:
        return _failure(error)
