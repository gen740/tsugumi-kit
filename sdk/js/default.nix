{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
pkgs.callPackage ../../nix/js-card.nix {
  inherit (deps)
    portCodegen
    cardInterface
    paramValues
    publishApi
    ;
  inherit (deps) runtime;
  libnode = pkgs.callPackage ../../nix/js-libnode.nix { };
}
// {
  inherit (deps.views) dawnView;
  views = deps.views.common;
}
