{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.javaSdk;
in
{
  run = sdk.javaCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
