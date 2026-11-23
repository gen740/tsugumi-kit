{ pkgs }:
builtins.mapAttrs (language: _: import (./sdk + "/${language}") { inherit pkgs; }) {
  cpp = null;
  input = null;
  csharp = null;
  go = null;
  java = null;
  js = null;
  julia = null;
  python = null;
  r = null;
  ruby = null;
  rust = null;
}
// {
  cppRuntime = (import ./nix/sdk-dependencies.nix { inherit pkgs; }).runtime;
}
