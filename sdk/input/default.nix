{ pkgs }:
{
  artifact =
    source:
    assert builtins.isPath source || pkgs.lib.isDerivation source;
    {
      __tsugumiArtifact = "${source}";
    };

  input =
    { outputs, values }:
    let
      interface = import ../../nix/card-interface.nix { inherit pkgs; } { inherit outputs; };
      data = pkgs.writeText "tsugumi-input-values.json" (builtins.toJSON values);
    in
    pkgs.runCommand "tsugumi-input-card"
      {
        nativeBuildInputs = [
          pkgs.flatbuffers
          (pkgs.python3.withPackages (_: [ (pkgs.callPackage ../../nix/port-codegen.nix { }) ]))
        ];
      }
      ''
        python3 ${./build.py} "${interface}/_outputs.fbs" "${data}" \
          "${pkgs.flatbuffers.src}/reflection/reflection.fbs" "$out/share/tsugumi/input"
      '';
}
