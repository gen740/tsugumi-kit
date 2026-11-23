use tsugumi::{Artifact, Failure, FailureKind, Inputs, Output, Result, send_value};

fn run(input: Inputs<'_>) -> Result<Output> {
    let path = "numbered.txt";
    let value = input.text.value();
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
