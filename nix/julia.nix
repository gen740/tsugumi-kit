{
  perSystem =
    { pkgs, kit, ... }:
    let
      template = ../templates/julia-write-text/definitions;
      evaluate = (import ../tests/card-eval.nix { inherit pkgs; }).call;
      write = evaluate (template + "/write") { };
    in
    {
      checks.juliaAbi = pkgs.callPackage ../tests/julia/check.nix { inherit kit; };
      checks.juliaWriteTextTemplate = pkgs.runCommand "tsugumi-julia-template-check" { } ''
        test -s "${write.run}/lib/main.so"
        test -s "${write.api}/share/tsugumi/api/README.md"
        touch "$out"
      '';
    };
}
