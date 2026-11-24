{
  perSystem =
    { pkgs, kit, ... }:
    let
      template = ../templates/r-write-text/definitions;
      evaluate = (import ../tests/card-eval.nix { inherit pkgs; }).call;
      write = evaluate (template + "/write") { };
    in
    {
      checks.rAbi = pkgs.callPackage ../tests/r { inherit kit; };
      checks.rWriteTextTemplate = pkgs.runCommand "tsugumi-r-template-check" { } ''
        test -s "${write.run}/lib/main.so"
        test -s "${write.api}/share/tsugumi/api/README.md"
        touch "$out"
      '';
    };
}
