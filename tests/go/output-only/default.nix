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
    inherit (params) src;
    name = "tsugumi-go-output-only-card";
  };
  views = sdk.views;
}
