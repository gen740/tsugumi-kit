{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    let
      # A nixpkgs pin other than the flake's, standing in for a project's pin.
      projectPkgs = import (builtins.fetchTarball {
        url = "https://github.com/NixOS/nixpkgs/archive/c78b9f9c117e3ad7a6c1a161a48dc388693049b0.tar.gz";
        sha256 = "sha256-0RIIMBi/qnDlcr+Xxjb00sXeuvC1mqIdL23Mwhnn6hk=";
      }) { inherit system; };
      projectKit = import ../. { pkgs = projectPkgs; };
      checkTextTemplate =
        name: definitionDir: libraries:
        let
          # These starters connect a required Text input to a required File output.
          contract = ../tests/templates/text-to-file;
          definition = import definitionDir {
            inputs.tsugumi = self.lib.${system}.bind { inherit contract definitionDir; };
          };
        in
        assert builtins.elem "text" (map (view: view.name) definition.views);
        pkgs.runCommand "tsugumi-${name}-template-check" { } ''
          for library in ${pkgs.lib.escapeShellArgs libraries}; do
            test -s "${definition.run}/lib/$library"
          done
          for view in ${pkgs.lib.escapeShellArgs (map (view: "${view.blob}") definition.views)}; do
            test -s "$view"
          done
          touch "$out"
        '';
    in
    {
      checks.jsAbi9 = pkgs.callPackage ./js-check.nix { kit = self.lib.${system}; };
      checks.cppAbi9 = pkgs.callPackage ./cpp-check.nix { kit = self.lib.${system}; };
      checks.javaAbi9 = pkgs.callPackage ./java-check.nix { kit = self.lib.${system}; };
      # The root entry builds with the caller's pkgs, not the kit's pin.
      checks.cppAbi9ProjectPkgs =
        assert projectKit.cppRuntime.drvPath != self.lib.${system}.cppRuntime.drvPath;
        projectPkgs.callPackage ./cpp-check.nix { kit = projectKit; };
      checks.goNormalizeTextTemplate =
        checkTextTemplate "go-normalize-text" ../templates/go-normalize-text/normalize
          [
            "main.so"
            "go-card.so"
          ];
      checks.rustNumberLinesTemplate =
        checkTextTemplate "rust-number-lines" ../templates/rust-number-lines/number
          [ "main.so" ];
      checks.javaWriteTextTemplate =
        checkTextTemplate "java-write-text" ../templates/java-write-text/write
          [
            "main.so"
            "card.jar"
          ];
    };
}
