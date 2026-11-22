{ inputs, pkgs, ... }:
let
  sdk = inputs.tsugumi.cppSdk;
in
{
  run = sdk.cppCard {
    src = ./src;
    buildInputs = [ pkgs.zlib ];
    tools = [
      pkgs.coreutils
      pkgs.testTool
    ];
    cxxflags = [
      "-DABI9_TEST_CARD"
      "-lz"
    ];
  };
  views = sdk.views;
}
