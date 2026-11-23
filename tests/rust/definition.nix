{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/rust> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./interface/_inputs.fbs;
    outputs = ./interface/_outputs.fbs;
  };
in
{
  run = sdk.rustCard (
    common
    // {
      src = ./card;
      sourcePrefix = "card";
      buildInputs = [ pkgs.zlib ];
    }
  );
  api = sdk.rustApi common;
  views = sdk.views;
}
