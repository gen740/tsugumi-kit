use tsugumi::{Failure, FailureKind, Inputs, Output, Result, require_input, send_value};

fn run(input: Inputs<'_>) -> Result<Output> {
    let path = "numbered.txt";
    let source = require_input("text", input.text)?;
    let value: Option<&str> = source.value().into();
    let value =
        value.ok_or_else(|| Failure::new(FailureKind::invalid_request, "text.value is absent"))?;
    let text: String = value
        .lines()
        .enumerate()
        .map(|(index, line)| format!("{}: {line}\n", index + 1))
        .collect();
    std::fs::write(path, text)
        .map_err(|err| Failure::new(FailureKind::output_invalid, format!("{path}: {err}")))?;
    send_value(format!("Wrote {path}"))?;

    Ok(Output { file: path.into() })
}

tsugumi::export!(run);
