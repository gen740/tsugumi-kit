{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.juliaAbi9 = pkgs.callPackage ../tests/julia/check.nix {
        kit = self.lib.${system};
      };
    };
}
