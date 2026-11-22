import math
import os
import threading
from datetime import date
from pathlib import Path

import tsugumi
from card import Input, Output
from inputs.table.Detail import Detail as InputDetail
from outputs.file.File import FileT
from outputs.number.Number import NumberT
from outputs.number.Position import PositionT
from outputs.table.Count import CountT
from outputs.table.Data import DataT
from outputs.table.Detail import Detail
from outputs.table.Row import RowT
from outputs.with_.Directory import DirectoryT
from packaging.version import Version
from plotly import graph_objects as go
from tsugumi_pth_probe import loaded

assert Path(os.environ["MPLCONFIGDIR"]) == Path.home() / ".matplotlib"
assert math.sqrt(4) == 2
assert Version("1.0rc1") < Version("1.0")
assert loaded
load_marker = Path("loaded")
assert not load_marker.exists()
load_marker.touch()

display_thread = None


def _display_later(fd):
    assert os.read(fd, 1) == b"!"
    assert tsugumi.cancelled()
    assert tsugumi.remaining_ms() == 0
    tsugumi.send("outside")


def _check_active_queries():
    results = []
    thread = threading.Thread(
        target=lambda: results.extend((tsugumi.cancelled(), tsugumi.remaining_ms()))
    )
    thread.start()
    thread.join()
    assert results == [False, 500]


def _check_blocking_queries(fd, ack_fd):
    def acknowledge():
        for _ in range(2):
            assert os.read(fd, 1) == b"!"
            os.write(ack_fd, b"!")

    thread = threading.Thread(target=acknowledge)
    thread.start()
    assert not tsugumi.cancelled()
    assert tsugumi.remaining_ms() == 500
    thread.join()


def _check_throwing_queries():
    for query, message in (
        (tsugumi.cancelled, "The host cancellation query failed"),
        (tsugumi.remaining_ms, "The host deadline query failed"),
    ):
        try:
            query()
        except RuntimeError as error:
            assert str(error) == message
        else:
            raise AssertionError(f"{query.__name__} did not raise RuntimeError")


def _show():
    tsugumi.send('résumé\n"')
    tsugumi.send(tsugumi.Markdown("**text**"))
    tsugumi.send(2.5)
    tsugumi.send(tsugumi.Table(["n"], [[1]]))
    data = bytes([0, 13, 255])
    tsugumi.send(tsugumi.Image(data, alt="sample"))
    tsugumi.send(tsugumi.Plotly({"data": []}))
    figure = go.Figure(go.Scatter(x=[date(2026, 11, 22)], y=[2]))
    figure.layout.template = None
    tsugumi.send(tsugumi.Plotly(figure))
    tsugumi.send("custom", data)


def run(input: Input) -> Output:
    global display_thread
    match input.params.mode:
        case 1:
            _check_active_queries()
            _show()
            display_thread = threading.Thread(target=_display_later, args=(input.params.fd,))
            display_thread.start()
        case 2:
            display_thread.join()
        case 3:
            raise tsugumi.Failure("cancelled", "explicit failure", cell="fit")
        case 4:
            raise RuntimeError("standard exception")
        case 5:
            raise SystemExit(5)
        case 6:
            return Output()
        case 7:
            # A generated type can pack an absent required field.
            return Output(number=NumberT(position=PositionT()), table=DataT(), file=FileT())
        case 11:
            _check_blocking_queries(input.params.fd, input.params.ackFd)
        case 12:
            _check_throwing_queries()

    assert input.table.detailType == InputDetail.Note
    assert input.table.detail.text == b"sample"
    rows = []
    for item in input.table.rows:
        row = RowT()
        row.name = item.name
        row.count = item.count + 1
        row.rowDetailType = Detail.Count
        row.rowDetail = CountT(value=row.count)
        rows.append(row)
    table = DataT()
    table.rows = rows
    table.detailType = Detail.Count
    table.detail = CountT()
    table.detail.value = rows[0].count

    position = input.from_.position
    assert (position.x, position.y) == (39, -200)
    number = NumberT(
        value=position.x + input.from__.value + rows[0].count,
        position=PositionT(x=position.x, y=position.y),
    )
    file = FileT()
    if input.file is None:
        path = Path("result.txt")
        path.write_text(str(number.value))
        file.value = path
    else:
        assert Path(input.file.value).read_text() == "sample input"
        file.value = input.file.value
    directory = None
    if input.directory is not None:
        assert (Path(input.directory.value) / "sample.txt").read_text() == "sample directory"
        directory = DirectoryT(value=input.directory.value)

    match input.params.mode:
        case 8:
            table.detailType = Detail.NONE
        case 9:
            table.detail = None
        case 10:
            rows[0].rowDetail = NumberT(value=42)

    return Output(number=number, table=table, file=file, with_=directory)
