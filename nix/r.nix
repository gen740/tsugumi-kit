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
      checks.rAbi9 = pkgs.callPackage ../tests/r {
        rCard = self.lib.${system}.rCard;
        runtime = config.packages.cppRuntime;
      };
    };
}
