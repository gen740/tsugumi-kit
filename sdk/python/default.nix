{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
pkgs.callPackage ./card.nix {
  inherit (deps) portCodegen cardInterface publishApi;
  inherit (deps) runtime ipaexGothic;
}
// {
  views = deps.views.plotlyViews;
}
