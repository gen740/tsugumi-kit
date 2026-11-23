# Number lines with Rust

An Input Card supplies text to a Rust Card that numbers the lines, writes
`numbered.txt` in a new Artifact and returns it on the `file` port.

```text
settings.text → number.text
                number.file → downstream
```

With the default two lines, the file contains `1: alpha` and `2: beta`. The
Card accepts LF and CRLF input, writes LF line endings and displays the
output filename live. Change `settings.value` to try another input.

Use `templates/rust-number-lines` as the Git import subdirectory.
[Template guide](../../docs/templates.md) · [Rust source](definitions/number/src/src/lib.rs)

The Card build publishes its typed API under `share/tsugumi/api/`.
[Rust SDK guide](../../docs/rust-sdk.md)
