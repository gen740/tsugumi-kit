# Write a note with C#

This Card writes its build-time `text` parameter to `note.txt` in a new
Artifact and returns it on the `file` port. Set `keep_output = false;` in
`note/params.nix` to keep the Artifact only while its run runs.

Use `templates/csharp-write-note` as the Git import subdirectory.
[Template guide](../../docs/templates.md) · [C# SDK guide](../../docs/csharp-sdk.md)
