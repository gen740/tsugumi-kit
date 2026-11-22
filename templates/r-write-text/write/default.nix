{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.rSdk;
in
{
  run = sdk.rCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
