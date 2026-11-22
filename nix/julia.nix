{ self, ... }:
{
  perSystem =
    {
      config,
      pkgs,
      system,
      ...
    }:
    {
      checks.juliaAbi9 = pkgs.callPackage ../tests/julia/check.nix {
        juliaCard = self.lib.${system}.juliaCard;
        runtime = config.packages.cppRuntime;
      };
    };
}
