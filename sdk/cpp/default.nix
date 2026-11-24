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
// {
  inherit (deps.views) dawnView;
  views = deps.views.common;
}
