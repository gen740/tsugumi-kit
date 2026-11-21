{ inputs, pkgs, ... }:
{
  run = inputs.tsugumi.cppCard {
    src = ./src;
    buildInputs = [ pkgs.zlib ];
    cxxflags = [
      "-DABI9_TEST_CARD"
      "-lz"
    ];
  };
}
