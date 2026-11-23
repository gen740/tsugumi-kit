{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/go> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ./interface/_outputs.fbs;
    params = ./interface/params.nix;
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
