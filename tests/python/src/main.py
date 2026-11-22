import math
import os
import subprocess
import threading
import warnings
from datetime import date
from pathlib import Path

import tsugumi
from card import Input, Output
from inputs.table.Detail import Detail as InputDetail
from inputs.arrival.tsugumi.merge.t0.Temperature import TemperatureT as InputTemperature
from inputs.arrival.tsugumi.merge.t1.Reading import ReadingT as InputReading
from outputs.arrival.tsugumi.merge.t0.Temperature import TemperatureT
from outputs.arrival.tsugumi.merge.t1.Reading import ReadingT
from outputs.number.Number import NumberT
from outputs.number.Position import PositionT
from outputs.table.Count import CountT
from outputs.table.Data import DataT
from outputs.table.Detail import Detail
from outputs.table.Row import RowT
from matplotlib.figure import Figure
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


def _check_matplotlib():
    fig = Figure()
    title = fig.suptitle("Temperature 温度")
    families = title.get_fontfamily()
    from tsugumi.mpl import figure

    assert Path(figure.__code__.co_filename).with_name("ipaexg.ttf").is_file()
    with warnings.catch_warnings():
        warnings.filterwarnings("error", message=r"Glyph .* missing from font")
        image = figure(fig)
    assert image.data.startswith(b"\x89PNG\r\n\x1a\n")
    assert title.get_fontfamily() == families


def _show():
    tsugumi.send('résumé\n"')
    tsugumi.send(tsugumi.Markdown("**text**"))
    tsugumi.send(2.5)
    tsugumi.send(tsugumi.Table(["n"], [[1]]))
    data = bytes([0, 13, 255])
    tsugumi.send(tsugumi.Image(data, alt="sample"))
    tsugumi.send(tsugumi.Points([1], [2], [3]))
    tsugumi.send(tsugumi.LineChart([tsugumi.LineSeries("id", "line", [2], [3])]))
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
            _check_matplotlib()
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
            return Output(number=NumberT(position=PositionT()), table=DataT(), file="result.txt")
        case 11:
            _check_blocking_queries(input.params.fd, input.params.ackFd)
        case 12:
            _check_throwing_queries()

        case 13:
            assert input.get("unwired") is None
            input.require("unwired")
        case 17:
            assert subprocess.run(["tsugumi-python-probe"]).returncode == 42
        case 18:
            assert "PATH" not in os.environ
            try:
                subprocess.run(["tsugumi-python-probe"])
            except FileNotFoundError:
                pass
            else:
                raise AssertionError("Found an undeclared tool")
        case 19:
            try:
                tsugumi.send(tsugumi.Points([1], [2], []))
            except ValueError:
                pass
            else:
                raise AssertionError("Accepted malformed points")
            try:
                tsugumi.send("arbitrary-view", b"test")
            except RuntimeError as error:
                assert str(error) == "The host rejected the display"
            else:
                raise AssertionError("Ignored transport rejection")

    number_input = input.require("from")
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

    assert input.get("from") is number_input
    position = number_input.position
    assert (position.x, position.y) == (39, -200)
    number = NumberT(
        value=position.x + input.from__.value + rows[0].count,
        position=PositionT(x=position.x, y=position.y),
    )
    if input.file is None:
        path = Path("result.txt")
        path.write_text(str(number.value))
        file = path
    else:
        assert Path(input.file).read_text() == "sample input"
        file = input.file
    directory = None
    if input.directory is not None:
        assert (Path(input.directory) / "sample.txt").read_text() == "sample directory"
        directory = input.directory

    match input.params.mode:
        case 8:
            table.detailType = Detail.NONE
        case 9:
            table.detail = None
        case 10:
            rows[0].rowDetail = NumberT(value=42)

    arrival = None
    if isinstance(input.arrival, InputTemperature):
        arrival = TemperatureT(value=input.arrival.value + 1)
    elif isinstance(input.arrival, InputReading):
        arrival = ReadingT(name=input.arrival.name)
    else:
        assert input.get("arrival") is None

    if input.params.mode == 16:
        file = "bad\0path"
    if input.params.mode == 20:
        arrival = number

    return Output(number=number, table=table, file=file, with_=directory, arrival=arrival)
