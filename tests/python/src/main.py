import math
import os
import threading
from pathlib import Path

import tsugumi
from card import Input, Output
from inputs.table.Detail import Detail as InputDetail
from outputs.file.File import FileT
from outputs.number.Number import NumberT
from outputs.table.Count import CountT
from outputs.table.Data import DataT
from outputs.table.Detail import Detail
from outputs.table.Row import RowT

assert Path(os.environ["MPLCONFIGDIR"]) == Path.home() / ".matplotlib"
assert math.sqrt(4) == 2
load_marker = Path("loaded")
assert not load_marker.exists()
load_marker.touch()

display_thread = None


def _display_later(fd):
    assert os.read(fd, 1) == b"!"
    assert tsugumi.cancelled()
    assert tsugumi.remaining_ms() == 0
    tsugumi.display("outside")


def _show():
    tsugumi.display('résumé\n"')
    tsugumi.display(tsugumi.Markdown("**text**"), record=True)
    tsugumi.display(2.5)
    tsugumi.display(tsugumi.Table(["n"], [[1]]))
    data = bytes([0, 13, 255])
    tsugumi.display(tsugumi.Image(data, alt="sample"))
    tsugumi.display(tsugumi.Points([1], [1], [1], [0x123456]))
    tsugumi.display(tsugumi.LineChart([tsugumi.LineSeries("id", "line", [2], [2])]))
    tsugumi.display(tsugumi.Plotly({"data": []}))
    tsugumi.display(tsugumi.VegaLite({"mark": "point"}))
    tsugumi.display(tsugumi.Module("custom", {"key": 1}, {"sample": data}), record=True)


def run(input: Input) -> Output:
    global display_thread
    match input.params.mode:
        case 1:
            assert not tsugumi.cancelled()
            assert tsugumi.remaining_ms() == 500
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
            return Output(number=NumberT(), table=DataT(), file=FileT())

    assert input.table.detailType == InputDetail.Note
    assert input.table.detail.text == b"sample"
    rows = []
    for item in input.table.rows:
        row = RowT()
        row.name = item.name
        row.count = item.count + 1
        rows.append(row)
    table = DataT()
    table.rows = rows
    table.detailType = Detail.Count
    table.detail = CountT()
    table.detail.value = rows[0].count

    number = NumberT()
    number.value = input.number.value + rows[0].count
    file = FileT()
    if input.file is None:
        path = Path("result.txt")
        path.write_text(str(number.value))
        file.value = str(path)
    else:
        # flatc's string accessors return bytes.
        assert Path(os.fsdecode(input.file.value)).read_text() == "sample input"
        file.value = input.file.value
    return Output(number=number, table=table, file=file)
