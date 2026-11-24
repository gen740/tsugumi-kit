# Write text with Java

The Java Card displays its text parameter and writes it to `message.txt` as
UTF-8 in a new Artifact on the `file` port. Edit `definitions/write/params.nix` to change the value.
Set `keep_output = false;` there to keep the Artifact only while its run runs.

Git import subdirectory `templates/java-write-text`.
[Template guide](../../docs/templates.md) · [Java source](definitions/write/src/card/Main.java)
