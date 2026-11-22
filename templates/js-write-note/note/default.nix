{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.jsSdk;
in
{
  run = sdk.jsCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
