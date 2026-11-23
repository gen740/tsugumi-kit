{
  perSystem =
    { pkgs, kit, ... }:
    {
      checks.rustAbi = pkgs.callPackage ../tests/rust {
        inherit kit;
      };
    };
}
