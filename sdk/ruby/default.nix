{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
import ../../nix/ruby-card.nix {
  inherit (deps)
    portCodegen
    cardInterface
    paramValues
    publishApi
    ;
  inherit pkgs;
  inherit (deps) runtime;
}
// {
  inherit (deps.views) dawnView;
  views = deps.views.common;
}
