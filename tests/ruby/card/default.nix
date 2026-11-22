{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.rubySdk;
in
{
  run = sdk.rubyCard {
    inherit schemas;
    src = ./src;
  };
  views = sdk.views;
}
