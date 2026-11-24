# Write text with Julia

Edit the text in `definitions/write/params.nix`. The write Card
writes the text to `note.txt` and returns it on the Artifact output `file`. Set `keep_output` to `false`
there to remove the Artifact when its run ends.

Use `templates/julia-write-text` as the Git import subdirectory.
[Template guide](../../docs/templates.md) · [Julia SDK guide](../../docs/julia-sdk.md)
