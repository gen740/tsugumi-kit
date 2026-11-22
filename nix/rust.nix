{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.rustAbi = pkgs.callPackage ../tests/rust {
        kit = self.lib.${system};
      };
    };
}
