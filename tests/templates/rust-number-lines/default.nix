{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/rust> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/rust-number-lines/number/_outputs.fbs;
  };
in
{
  run = sdk.rustCard (
    common
    // {
      src = ../../../templates/rust-number-lines/number/src;
      sourcePrefix = "src";
    }
  );
  api = sdk.rustApi common;
  views = sdk.views;
}
