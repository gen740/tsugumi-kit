{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
import ./card.nix {
  inherit (deps)
    portCodegen
    cardInterface
    paramValues
    publishApi
    ;
  inherit pkgs;
  abi = ../../abi;
}
// (import ../input { inherit pkgs; })
// {
  views = deps.views.common;
}
