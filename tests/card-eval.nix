{ pkgs }:
{
  inputs =
    directory:
    pkgs.lib.mapAttrs'
      (
        file: _:
        let
          name = pkgs.lib.removeSuffix ".fbs" file;
        in
        pkgs.lib.nameValuePair name (
          {
            type = pkgs.writeText file (builtins.readFile (directory + "/${file}"));
          }
          // pkgs.lib.optionalAttrs (
            !(builtins.elem name [
              "arrival"
              "draft"
            ])
          ) { exports = { }; }
        )
      )
      (
        pkgs.lib.filterAttrs (file: kind: kind == "regular" && pkgs.lib.hasSuffix ".fbs" file) (
          builtins.readDir directory
        )
      );

  call =
    definition:
    {
      params ? { },
      inputs ? { },
    }:
    builtins.scopedImport {
      __nixPath = [
        {
          prefix = "nixpkgs";
          path = pkgs.path;
        }
        {
          prefix = "tsugumi-kit";
          path = ../.;
        }
      ];
      # In pure or cross-system flake evaluation, reproduce the target pkgs
      # that the core supplies by setting builtins.currentSystem.
      import =
        path:
        if path == pkgs.path then
          args: builtins.import path ({ system = pkgs.stdenv.hostPlatform.system; } // args)
        else
          builtins.import path;
    } definition { inherit params inputs; };
}
