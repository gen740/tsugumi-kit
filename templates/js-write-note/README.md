# Write a note with JavaScript

This Card writes its `text` parameter to `note.txt` and returns it on the
`file` Artifact output. It displays the byte count after the write completes.
Set `keep_output = false;` in `definitions/note/params.nix` to drop the
Artifact when its run ends.

Use `templates/js-write-note` as the Git import subdirectory.
[Template guide](../../docs/templates.md) · [JavaScript SDK guide](../../docs/js-sdk.md)
