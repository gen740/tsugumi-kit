use tsugumi::{Artifact, Failure, FailureKind, Inputs, Output, PARAMS, Result, send_value};

fn run(_input: Inputs) -> Result<Output> {
    let path = "numbered.txt";
    let value = PARAMS.value;
    let (artifact, directory) = Artifact::dir()?;
    let text: String = value
        .lines()
        .enumerate()
        .map(|(index, line)| format!("{}: {line}\n", index + 1))
        .collect();
    std::fs::write(directory.join(path), text)
        .map_err(|err| Failure::new(FailureKind::output_invalid, format!("{path}: {err}")))?;
    send_value(format!("Wrote {path}"))?;

    Ok(Output {
        file: Some(artifact),
    })
}

tsugumi::export!(run);
