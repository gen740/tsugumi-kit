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
        inherit (self.lib.${system}) cppCard view;
        runtime = config.packages.cppRuntime;
      };
    };
}
