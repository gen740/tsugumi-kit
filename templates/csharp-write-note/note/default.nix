{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.csharpSdk;
in
{
  run = sdk.csharpCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
