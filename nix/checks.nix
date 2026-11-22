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
      checks.cppAbi9 = pkgs.callPackage ./cpp-check.nix {
        inherit (self.lib.${system}) cppSdk;
        runtime = config.packages.cppRuntime;
      };
    };
}
