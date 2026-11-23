{ pkgs }:
let
  deps = import ../../nix/sdk-dependencies.nix { inherit pkgs; };
in
import ./build.nix {
  inherit (deps) portCodegen cardInterface publishApi;
  inherit pkgs;
  cppRuntime = deps.runtime;
}
// {
  views = deps.views.common;
}
