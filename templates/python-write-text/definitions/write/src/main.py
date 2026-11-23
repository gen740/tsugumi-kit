import tsugumi
from card import Artifact, Input, Output


def run(input: Input) -> Output:
    text = input.require("text").value
    artifact, directory = Artifact.dir()
    path = directory / "message.txt"
    path.write_bytes((text or b"") + b"\n")
    tsugumi.send(path.name)
    return Output(file=artifact)
