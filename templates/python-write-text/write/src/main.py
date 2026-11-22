from pathlib import Path

import tsugumi
from card import Input, Output


def run(input: Input) -> Output:
    text = input.require("text").value
    path = Path("message.txt")
    path.write_bytes((text or b"") + b"\n")
    tsugumi.send(str(path))
    return Output(file=path)
