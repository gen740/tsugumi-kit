use tsugumi::{Failure, FailureKind, Inputs, Output, Result, outputs, send_value};

fn run(input: Inputs<'_>) -> Result<Output> {
    let path = "numbered.txt";
    let text: String = input
        .text
        .value()
        .lines()
        .enumerate()
        .map(|(index, line)| format!("{}: {line}\n", index + 1))
        .collect();
    std::fs::write(path, text)
        .map_err(|err| Failure::new(FailureKind::output_invalid, format!("{path}: {err}")))?;
    send_value(format!("Wrote {path}"))?;

    let mut file = outputs::File::default();
    file.value = path.into();
    Ok(Output { file })
}

tsugumi::export!(run);
