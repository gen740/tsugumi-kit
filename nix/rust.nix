{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.rustAbi9 = pkgs.callPackage ../tests/rust {
        kit = self.lib.${system};
      };
    };
}
