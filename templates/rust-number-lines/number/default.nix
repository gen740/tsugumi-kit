{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.rustSdk;
in
{
  run = sdk.rustCard {
    inherit schemas;
    src = ./src;
    sourcePrefix = "src";
  };
  views = sdk.views;
}
