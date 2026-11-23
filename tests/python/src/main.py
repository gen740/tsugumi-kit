from threading import Thread

import tsugumi
from card import Artifact, Input, Output, params
from inputs.arrival.Number import NumberT
from inputs.batch.Element import ElementT
from outputs.Child import ChildT
from outputs.Choice import Choice
from outputs.Listing import ListingT


def background():
    status, data = tsugumi.display.recv("background")
    if status == "message":
        tsugumi.display.send("background", data)


def run(input: Input) -> Output:
    item = input.require("arrival")
    assert item.file.open().is_dir() and input.require("direct").open() == item.file.open()
    envelope, = input.require("batch").items
    assert envelope.schema == b"4" * 64
    assert ElementT.InitFromPackedBuf(bytes(envelope.bytes)).value == 4
    assert params.flag and params.ratio == 1.0 and params.labels[0] == "日本語"
    assert not tsugumi.cancelled() and tsugumi.remaining_ms() == 500
    if isinstance(item, NumberT) and item.value < 0:
        return Output()
    artifact, directory = Artifact.dir()
    file = directory / "value.txt"
    file.write_text("test")
    upload = Artifact.upload(file)
    choice, _ = Artifact.dir()

    assert tsugumi.display.recv("request") == ("message", b"*")
    assert tsugumi.display.recv("request", 0) == ("timeout", None)
    assert tsugumi.display.recv("request", 1) == ("cancelled", None)
    tsugumi.display.send("listing", b"*")
    tsugumi.display.send("listing", b"*" * ((1 << 24) + 5))
    Thread(target=background, daemon=True).start()
    return Output(listing=ListingT(
        nested=ChildT(file=artifact),
        files=[upload, item.file],
        choiceType=Choice.File,
        choice=choice,
        value=params.offset + (item.value if isinstance(item, NumberT) else 20),
    ))
