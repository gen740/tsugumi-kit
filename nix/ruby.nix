{
  perSystem =
    {
      pkgs,
      kit,
      ...
    }:
    {
      checks.rubyAbi = pkgs.callPackage ../tests/ruby/check.nix { inherit kit; };
    };
}
