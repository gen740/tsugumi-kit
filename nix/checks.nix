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
        cppCard = self.lib.${system}.cppCard;
        runtime = config.packages.cppRuntime;
      };
    };
}
