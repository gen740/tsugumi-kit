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
    in
    {
      checks.cppAbi9 = pkgs.callPackage ./cpp-check.nix { kit = self.lib.${system}; };
      # The root entry builds with the caller's pkgs, not the kit's pin.
      checks.cppAbi9ProjectPkgs =
        assert projectKit.cppRuntime.drvPath != self.lib.${system}.cppRuntime.drvPath;
        projectPkgs.callPackage ./cpp-check.nix { kit = projectKit; };
    };
}
