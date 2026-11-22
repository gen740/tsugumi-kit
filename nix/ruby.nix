{ self, ... }:
{
  perSystem =
    {
      pkgs,
      system,
      ...
    }:
    {
      checks.rubyAbi9 = pkgs.callPackage ../tests/ruby/check.nix { kit = self.lib.${system}; };
    };
}
