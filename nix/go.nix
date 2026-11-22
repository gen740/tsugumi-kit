{ self, ... }:
{
  perSystem =
    {
      pkgs,
      config,
      system,
      ...
    }:
    {
      checks.goAbi9 = pkgs.callPackage ../tests/go/check.nix {
        kit = self.lib.${system};
        runtime = config.packages.cppRuntime;
      };
    };
}
