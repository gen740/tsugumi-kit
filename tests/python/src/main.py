import sys
from threading import Thread

import tsugumi
from card import Artifact, Input, Output, params
from inputs.arrival.Number import NumberT
from inputs.batch.Element import ElementT
from outputs.Child import ChildT
from outputs.Choice import Choice
from outputs.Listing import ListingT


def background():
    event = tsugumi.recv()
    if event.kind == "message":
        tsugumi.send(tsugumi.view_open("background").session, event.data)


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
    # The private main modules see one shared CPython sys module.
    if params.offset == 100:
        assert not hasattr(sys, "_tsugumi_shared_test")
        sys._tsugumi_shared_test = __name__
    else:
        assert sys._tsugumi_shared_test != __name__
        del sys._tsugumi_shared_test
    artifact, directory = Artifact.dir()
    file = directory / "value.txt"
    file.write_text("test")
    upload = Artifact.upload(file)
    choice, _ = Artifact.dir(keep=False)

    view = tsugumi.view_open(
        "listing", tsugumi.ViewMode.WINDOW,
        tsugumi.ViewFlags.HIDE_CURSOR | tsugumi.ViewFlags.LOCK_POINTER, 640, 480,
    )
    route = tsugumi.route_register(view.session)
    assert tsugumi.recv() == tsugumi.ViewEvent("message", route, view.session, b"*")
    assert tsugumi.recv(0).kind == "timeout"
    assert tsugumi.recv(1).kind == "cancelled"
    tsugumi.route_unregister(route)
    tsugumi.send(view.session, b"*")
    tsugumi.send(view.session, b"*" * (1 << 24))
    try:
        tsugumi.send(view.session, b"*" * ((1 << 24) + 5))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Oversize message was accepted")
    tsugumi.view_close(view.session)
    try:
        tsugumi.route_register(view.session)
    except tsugumi.WindowClosed:
        pass
    else:
        raise AssertionError("Registered a route on a closed window")
    assert tsugumi.recv() == tsugumi.ViewEvent("close", view.route, view.session, b"")
    Thread(target=background, daemon=True).start()
    return Output(listing=ListingT(
        nested=ChildT(file=artifact),
        files=[upload, item.file],
        choiceType=Choice.File,
        choice=choice,
        value=params.offset + (item.value if isinstance(item, NumberT) else 20),
    ))
