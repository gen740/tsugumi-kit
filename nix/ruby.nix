{ self, ... }:
{
  perSystem =
    {
      pkgs,
      system,
      ...
    }:
    {
      checks.rubyAbi = pkgs.callPackage ../tests/ruby/check.nix { kit = self.lib.${system}; };
    };
}
