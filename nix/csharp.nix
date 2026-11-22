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
      checks.csharpAbi9 = pkgs.callPackage ../tests/csharp/check.nix {
        inherit (self.lib.${system}) csharpCard;
        inherit (config.packages) cppRuntime;
      };
    };
}
