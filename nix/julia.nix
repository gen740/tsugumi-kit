{
  perSystem =
    { pkgs, kit, ... }:
    {
      checks.juliaAbi = pkgs.callPackage ../tests/julia/check.nix {
        inherit kit;
      };
    };
}
