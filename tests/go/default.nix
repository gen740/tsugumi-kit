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
    buildInputs = [ pkgs.zlib ];
  };
  views = sdk.views;
}
