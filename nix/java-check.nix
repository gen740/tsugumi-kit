{ pkgs, kit }:
let
  schemas = ../tests/contract;
  common = {
    inputs = schemas + "/_inputs.fbs";
    outputs = schemas + "/_outputs.fbs";
    params = import (schemas + "/params.nix");
  };
  offsetJar =
    pkgs.runCommand "tsugumi-java-test-dependency.jar" { nativeBuildInputs = [ pkgs.jdk21_headless ]; }
      ''
        mkdir classes
        cp ${../tests/java/Offset.java} Offset.java
        javac -d classes Offset.java
        jar --create --date=1980-01-01T00:00:02Z --file "$out" -C classes .
      '';
  sort = pkgs.writeShellScriptBin "tsugumi-test-sort" ''
    exec ${pkgs.coreutils}/bin/sort "$@"
  '';
  card =
    params:
    kit.java.javaCard (
      common
      // {
        inherit params;
        src = ../tests/java/card/src;
        jars = [ offsetJar ];
        tools = [ sort ];
      }
    );
  api = params: kit.java.javaApi (common // { inherit params; });
  changed = common.params // {
    offset = 200;
  };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ./contract-check.nix { inherit pkgs; }) {
  name = "java";
  card = card common.params;
  secondCard = card changed;
  api = api common.params;
  runtime = kit.cppRuntime;
}
