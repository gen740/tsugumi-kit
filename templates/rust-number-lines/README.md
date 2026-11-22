# Number lines with Rust

An Input Card supplies text to a Rust Card that numbers the lines, writes
`numbered.txt` and returns it on the `file` port. **Targets Card ABI 9 revision 2.**

```text
settings.text → number.text
                number.file → downstream
```

With the default two lines, the file contains `1: alpha` and `2: beta`. The
Card accepts LF and CRLF input, writes LF line endings and displays the
output filename live. Change `settings.value` to try another input.

Use `templates/rust-number-lines` as the Git import subdirectory.
[Template guide](../../docs/templates.md) · [Rust source](number/src/src/lib.rs)

The Card build publishes its typed API under `share/tsugumi/api/`.
[Rust SDK guide](../../docs/rust-sdk.md)
