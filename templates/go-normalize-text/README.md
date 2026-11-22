# Normalize text with Go

An Input Card supplies text to a Go Card that collapses whitespace, writes
`normalized.txt` and returns it on the `file` port. **Targets Card ABI 9 revision 2.**

```text
settings.text → normalize.text
                normalize.file → downstream
```

The default text, `alpha   beta` followed by a tab and `gamma`, produces
`alpha beta gamma` with a final newline. The Card joins words separated by
Unicode whitespace with single spaces and displays the output filename live.
Change `settings.value` to try another input.

Use `templates/go-normalize-text` as the Git import subdirectory.
[Template guide](../../docs/templates.md) · [Go source](normalize/src/card.go)

The Card build publishes its typed API under `share/tsugumi/api/`.
[Go SDK guide](../../docs/go-sdk.md)
