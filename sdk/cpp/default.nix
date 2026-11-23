{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
pkgs.callPackage ../../nix/cpp-card.nix {
  inherit (deps) portCodegen cardInterface publishApi;
  inherit (deps) runtime;
}
// {
  views = deps.views.common;
}
