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
// (import ../input { inherit pkgs; })
// {
  views = deps.views.common;
}
