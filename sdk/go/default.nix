{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
import ../../nix/go-card.nix {
  inherit (deps) portCodegen cardInterface publishApi;
  inherit pkgs;
  inherit (deps) runtime;
}
// (import ../input { inherit pkgs; })
// {
  views = deps.views.common;
}
