{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.juliaAbi = pkgs.callPackage ../tests/julia/check.nix {
        kit = self.lib.${system};
      };
    };
}
