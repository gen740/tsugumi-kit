# Number lines with Rust

The Rust Card numbers the lines in its text parameter and returns
`numbered.txt` on its Artifact output `file`. Edit `definitions/number/params.nix` to change the value. Set `keep_output = false;` there to drop the Artifact when its run ends.

Git import subdirectory `templates/rust-number-lines`.
[Template guide](../../docs/templates.md) · [Rust source](definitions/number/src/src/lib.rs)
