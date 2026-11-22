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
      checks.rubyAbi9 = pkgs.callPackage ../tests/ruby/check.nix {
        rubyCard = self.lib.${system}.rubyCard;
        runtime = config.packages.cppRuntime;
      };
    };
}
