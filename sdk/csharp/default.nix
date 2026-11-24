{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
import ./build.nix {
  inherit (deps)
    portCodegen
    cardInterface
    paramValues
    publishApi
    ;
  inherit pkgs;
  cppRuntime = deps.runtime;
}
// {
  inherit (deps.views) dawnView;
  views = deps.views.common;
}
