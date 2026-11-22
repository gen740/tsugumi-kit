{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.juliaSdk;
in
{
  run = sdk.juliaCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
