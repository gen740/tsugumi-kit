{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/rust> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ./interface/_outputs.fbs;
    params = ./interface/params.nix;
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
