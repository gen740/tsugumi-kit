{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
pkgs.callPackage ../../nix/java-card.nix {
  inherit (deps) portCodegen cardInterface publishApi;
  inherit (deps) runtime;
}
// (import ../input { inherit pkgs; })
// {
  views = deps.views.common;
}
