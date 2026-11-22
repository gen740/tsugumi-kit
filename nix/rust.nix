{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.rustAbi9 = pkgs.callPackage ../tests/rust {
        rustCard = self.lib.${system}.rustCard;
      };
    };
}
