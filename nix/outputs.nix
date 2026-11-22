{
  self,
  config,
  lib,
  withSystem,
  ...
}:
{
  flake.lib = lib.genAttrs config.systems (
    system: withSystem system ({ pkgs, ... }: import ../. { inherit pkgs; })
  );

  perSystem =
    { pkgs, system, ... }:
    {
      formatter = pkgs.nixfmt;
      packages.cppRuntime = self.lib.${system}.cppRuntime;
    };
}
