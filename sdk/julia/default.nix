{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
import ./card.nix {
  inherit (deps) portCodegen cardInterface publishApi;
  inherit pkgs;
  inherit (deps) runtime;
}
// {
  views = deps.views.common;
}
