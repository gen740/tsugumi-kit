{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/rust> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./_inputs.fbs;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.rustCard (
    common
    // {
      src = ./src;
      sourcePrefix = "src";
    }
  );
  api = sdk.rustApi common;
  views = sdk.views;
}
