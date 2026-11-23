# Write text with Java

An Input Card supplies text to a Java Card. The Java Card displays the text,
writes it to `message.txt` as UTF-8 in a new Artifact and returns it on the `file` port.

```text
settings.text → write.text
                write.file → downstream
```

Change `settings.value` to try a different message.

Use `templates/java-write-text` as the Git import subdirectory.
[Template guide](../../docs/templates.md) · [Java source](definitions/write/src/card/Main.java)
