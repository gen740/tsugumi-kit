{
  perSystem =
    { pkgs, kit, ... }:
    let
      template = ../templates/ruby-write-text/definitions;
      evaluate = (import ../tests/card-eval.nix { inherit pkgs; }).call;
      write = evaluate (template + "/write") { };
    in
    {
      checks.rubyAbi = pkgs.callPackage ../tests/ruby/check.nix { inherit kit; };
      checks.rubyWriteTextTemplate = pkgs.runCommand "tsugumi-ruby-template-check" { } ''
        test -s "${write.run}/lib/main.so"
        test -s "${write.api}/share/tsugumi/api/README.md"
        touch "$out"
      '';
    };
}
