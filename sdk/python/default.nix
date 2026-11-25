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
  pythonRuntime = python: pkgs.callPackage ./runtime.nix { inherit python; };
}
// {
  inherit (deps.views) dawnView;
  views = deps.views.plotlyViews;
}
