{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
pkgs.callPackage ../../nix/cpp-card.nix {
  inherit (deps)
    portCodegen
    cardInterface
    paramValues
    publishApi
    abiHeaders
    ;
  inherit (deps) runtime;
}
// (import ../input { inherit pkgs; })
// {
  views = deps.views.common;
}
