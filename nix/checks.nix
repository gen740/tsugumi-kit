{
  perSystem =
    {
      config,
      pkgs,
      kit,
      system,
      ...
    }:
    let
      inherit (pkgs) lib;
      pendingSdkChecks = import ./pending-sdk-checks.nix;
      enabled = sdk: !(builtins.elem sdk pendingSdkChecks);
      abiChecks = [
        "cpp"
        "csharp"
        "go"
        "java"
        "js"
        "julia"
        "python"
        "r"
        "ruby"
        "rust"
      ];
      apis = lib.concatMap (sdk: config.checks."${sdk}Abi".apis) (builtins.filter enabled abiChecks);
      # A nixpkgs pin other than the flake's, standing in for a project's pin.
      projectPkgs = import (builtins.fetchTarball {
        url = "https://github.com/NixOS/nixpkgs/archive/c78b9f9c117e3ad7a6c1a161a48dc388693049b0.tar.gz";
        sha256 = "sha256-0RIIMBi/qnDlcr+Xxjb00sXeuvC1mqIdL23Mwhnn6hk=";
      }) { inherit system; };
      projectKit = import ../. { pkgs = projectPkgs; };
      checkTextTemplate =
        name: definitionDir: libraries:
        let
          fixtureEval = import ../tests/card-eval.nix { inherit pkgs; };
          definition = fixtureEval.call definitionDir { };
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
      checks = {
        cppAbi = pkgs.callPackage ./cpp-check.nix { inherit kit; };
        cppAbiProjectPkgs =
          assert projectKit.cppRuntime.drvPath != kit.cppRuntime.drvPath;
          projectPkgs.callPackage ./cpp-check.nix { kit = projectKit; };
        sdkApi =
          assert import ../tests/param-validation.nix { inherit pkgs; };
          pkgs.runCommand "tsugumi-sdk-api-check" { } ''
            for api in ${lib.escapeShellArgs (map toString apis)}; do
              test ! -e "$api/lib/main.so"
              readme="$api/share/tsugumi/api/README.md"
              test -s "$readme"
              for section in Inputs Outputs Params; do
                grep -Fxq "## $section" "$readme"
              done
            done
            touch "$out"
          '';
      }
      // lib.optionalAttrs (enabled "js") {
        jsAbi = pkgs.callPackage ./js-check.nix { inherit kit; };
      }
      // lib.optionalAttrs (enabled "java") {
        javaAbi = pkgs.callPackage ./java-check.nix { inherit kit; };
        javaWriteTextTemplate = checkTextTemplate "java-write-text" ../templates/java-write-text/write [
          "main.so"
          "card.jar"
        ];
      }
      // lib.optionalAttrs (enabled "go") {
        goNormalizeTextTemplate =
          checkTextTemplate "go-normalize-text" ../templates/go-normalize-text/normalize
            [
              "main.so"
              "go-card.so"
            ];
      }
      // lib.optionalAttrs (enabled "rust") {
        rustNumberLinesTemplate =
          checkTextTemplate "rust-number-lines" ../templates/rust-number-lines/number
            [ "main.so" ];
      };
    };
}
