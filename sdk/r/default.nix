{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
pkgs.callPackage ./card.nix {
  inherit (deps)
    portCodegen
    cardInterface
    paramValues
    publishApi
    ;
  inherit (deps) runtime ipaexGothic;
}
// {
  inherit (deps.views) dawnView;
  views = deps.views.common;
}
