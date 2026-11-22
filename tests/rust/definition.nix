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
    src = ./card;
    sourcePrefix = "card";
    buildInputs = [ pkgs.zlib ];
  };
  views = sdk.views;
}
