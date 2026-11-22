# Write text with Python

An Input Card supplies text to a Python Card that writes it to `message.txt`,
displays the filename, and returns the file on a Path port.
**Targets unreleased Card ABI 9, revision 2.**

```text
settings.text → write.text
                write.file → downstream
```

Copy the pair into a project and connect a Trigger to `settings.start`.
Change the `value` parameter on `settings` to write a different message. If
your importer copies only definitions, connect the two Cards as shown.

Git import subdirectory `templates/python-write-text`.
[Template guide](../../docs/templates.md) · [Python SDK](../../docs/python-sdk.md)
