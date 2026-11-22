# Go Card SDK

Use `inputs.tsugumi.goSdk.goCard { inherit schemas; src = ./src; }`
in a Card's `default.nix`. The generated module exposes `Inputs`,
`Output`, and `RequireInput[T]`. The build includes the generated API
in `share/tsugumi/api/`.

[Go SDK guide](../../docs/go-sdk.md) · [Go template](../../templates/go-normalize-text/)
