# Rust Card SDK

Use `inputs.tsugumi.rustSdk.rustCard { inherit schemas; src = ./src; }`
in a Card's `default.nix`. The generated crate exposes `Inputs`, `Output`,
`require_input`, and `export!`. The build includes the generated API in
`share/tsugumi/api/`.

[Rust SDK guide](../../docs/rust-sdk.md) · [Rust template](../../templates/rust-number-lines/)
