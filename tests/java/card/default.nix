{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/java> { inherit pkgs; };
  common = {
    inherit inputs params;
    outputs = ../interface/_outputs.fbs;
  };
in
{
  run = sdk.javaCard (
    common
    // {
      src = ./src;
      jars = [
        (pkgs.runCommand "tsugumi-java-test-dependency.jar"
          {
            nativeBuildInputs = [ pkgs.jdk21_headless ];
          }
          ''
            mkdir classes
            cp ${../Offset.java} Offset.java
            javac -d classes Offset.java
            jar --create --date=1980-01-01T00:00:02Z --file "$out" -C classes .
          ''
        )
      ];
      tools = [
        (pkgs.writeShellScriptBin "tsugumi-test-sort" ''
          exec ${pkgs.coreutils}/bin/sort "$@"
        '')
      ];
    }
  );
  api = sdk.javaApi common;
  views = sdk.views;
}
