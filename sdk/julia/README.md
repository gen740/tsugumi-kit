# Julia Cards

Use `inputs.tsugumi.juliaSdk.juliaCard { inherit schemas; src = ./src; }`
from a Card definition that takes `{ pkgs, params, inputs, schemas }`.
Write `main(input)` in `src/main.jl`.

- [Julia SDK guide](../../docs/julia-sdk.md)
- [Julia Card starter](../../templates/julia-write-text/)
- [Card SDK contract](../../docs/sdk-contract.md)
