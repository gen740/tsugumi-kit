# R Cards

Use `inputs.tsugumi.rSdk.rCard { inherit schemas; src = ./src; }`
from a Card definition that takes `{ pkgs, params, inputs, schemas }`.
Write `main(input)` in `src/main.R`.

- [R SDK guide](../../docs/r-sdk.md)
- [R Card starter](../../templates/r-write-text/)
- [Card SDK contract](../../docs/sdk-contract.md)
