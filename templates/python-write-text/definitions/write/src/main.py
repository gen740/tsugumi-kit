import tsugumi
from card import Artifact, Input, Output, params


def run(input: Input) -> Output:
    artifact, directory = Artifact.dir()
    path = directory / "message.txt"
    path.write_text(params.value + "\n", encoding="utf-8")
    tsugumi.send(path.name)
    return Output(file=artifact)
