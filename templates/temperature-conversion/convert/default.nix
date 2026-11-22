{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.cppSdk;
in
{
  run = sdk.cppCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
