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
        inherit (self.lib.${system}) goCard goCardSdk;
        runtime = config.packages.cppRuntime;
      };
    };
}
