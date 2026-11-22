{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.goSdk;
in
{
  run = sdk.goCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
