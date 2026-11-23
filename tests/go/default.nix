{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/go> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./interface/_inputs.fbs;
    outputs = ./interface/_outputs.fbs;
  };
in
{
  run = sdk.goCard (
    common
    // {
      src = ./src;
      buildInputs = [ pkgs.zlib ];
    }
  );
  api = sdk.goApi common;
  views = sdk.views;
}
