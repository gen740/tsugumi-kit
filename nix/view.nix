{ lib, runCommand }:
let
  kinds = [
    "js"
    "wasm"
  ];
  isEntry =
    entry:
    builtins.isString entry
    && entry != ""
    && !lib.hasPrefix "/" entry
    && !builtins.elem ".." (lib.splitString "/" entry);
  isView =
    view:
    view ? tsugumiView
    && builtins.elem (view.tsugumiView.kind or null) kinds
    && isEntry (view.tsugumiView.entry or null);
in
{
  # Copy the directory src to $out and declare the route the core takes to
  # load it: a JS module or a Wasm module at $out/<entry>.
  view =
    {
      kind,
      src,
      entry,
      name ? "tsugumi-${kind}-view",
    }:
    assert lib.assertMsg (builtins.elem kind kinds) ''view: kind must be "js" or "wasm"'';
    assert lib.assertMsg (isEntry entry) "view: entry must be a relative path inside $out";
    runCommand name { passthru.tsugumiView = { inherit kind entry; }; } ''
      cp -R --no-preserve=mode ${lib.escapeShellArg "${src}"} "$out"
      if [ ! -f "$out"/${lib.escapeShellArg entry} ]; then
        echo ${lib.escapeShellArg "view: ${entry} is not a file in src"} >&2
        exit 1
      fi
    '';

  # Reject a views map entry that does not declare its route.
  checkViews =
    builder: views:
    lib.mapAttrs (
      name: view:
      assert lib.assertMsg (isView view)
        "${builder}: view ${name} must declare passthru.tsugumiView = { kind = \"js\" | \"wasm\"; entry = ...; }; use lib.<system>.view";
      view
    ) views;
}
