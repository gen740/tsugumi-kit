{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.cppSdk;
in
{
  run = sdk.cppCard {
    inherit schemas;
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
