{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/cpp> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ../cpp/interface/_inputs.fbs;
    outputs = ../cpp/interface/_outputs.fbs;
  };
in
{
  run = sdk.cppCard (
    common
    // {
      src = ./src;
      buildInputs = [ inputs.mylib.package ];
      tools = [
        pkgs.coreutils
        (pkgs.writeShellScriptBin "tsugumi-test-tool" "exit 42")
      ];
      inherit (params) cxxflags;
    }
  );
  api = sdk.cppApi common;
  views = sdk.views;
}
