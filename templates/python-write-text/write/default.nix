{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.pythonSdk;
in
{
  run = sdk.pythonCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
