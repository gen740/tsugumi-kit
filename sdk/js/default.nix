{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
pkgs.callPackage ../../nix/js-card.nix {
  inherit (deps) portCodegen cardInterface publishApi;
  inherit (deps) runtime;
  libnode = pkgs.callPackage ../../nix/js-libnode.nix { };
}
// {
  views = deps.views.common;
}
