{ pkgs, runtime }:
let
  legacyHeaders = pkgs.lib.fileset.toSource {
    root = ../.;
    fileset = ../display.hh;
  };
  inherit (pkgs)
    lib
    stdenv
    flatbuffers
    nlohmann_json
    cppcodec
    ;
  inherit (stdenv.hostPlatform) isDarwin;
  julia =
    if isDarwin then
      # Avoid Julia's upstream install test suite when building this SDK.
      pkgs.julia-bin.overrideAttrs { doInstallCheck = false; }
    else
      pkgs.julia-bin;
  juliaLib = "${julia}/lib/libjulia${stdenv.hostPlatform.extensions.sharedLibrary}";
  project = ./.;
  manifest = builtins.fromTOML (builtins.readFile ./Manifest.toml);
  packages =
    lib.mapAttrs
      (name: source: {
        inherit source;
        inherit (builtins.head manifest.deps.${name}) uuid git-tree-sha1;
      })
      {
        JSON = pkgs.fetchFromGitHub {
          owner = "JuliaIO";
          repo = "JSON.jl";
          tag = "v${(builtins.head manifest.deps.JSON).version}";
          hash = "sha256-vl9/+PMSFTk3K/tQzVWxE2n+FAe4DlYcnpUUs4XMGRY=";
        };
        Parsers = pkgs.fetchFromGitHub {
          owner = "JuliaData";
          repo = "Parsers.jl";
          tag = "v${(builtins.head manifest.deps.Parsers).version}";
          hash = "sha256-dFgOV3oNFhfrVnzRseK+AcOgvmgUZVMkT3p94VMmAmU=";
        };
      };
  depot = pkgs.runCommand "tsugumi-julia-depot" { nativeBuildInputs = [ julia ]; } ''
    # Materialize the Pkg manifest's package trees in the read-only depot.
    # The worker provides the first, writable depot under HOME.
    export HOME="$TMPDIR"
    export JULIA_DEPOT_PATH="$TMPDIR/depot:"
    julia --startup-file=no --history-file=no ${./install-depot.jl} "$out" ${
      lib.escapeShellArgs (
        lib.concatLists (
          lib.mapAttrsToList (name: value: [
            name
            value.uuid
            value.git-tree-sha1
            value.source
          ]) packages
        )
      )
    }
  '';
in
{
  src,
  contract,
  entry ? "main.jl",
  sourcePrefix ? "src",
}:
assert lib.assertMsg (
  sourcePrefix == ""
  || lib.all (part: part != "" && part != "." && part != "..") (lib.splitString "/" sourcePrefix)
) "juliaCard: sourcePrefix must be a relative directory inside the Card definition";
pkgs.runCommandCC "tsugumi-julia-card"
  {
    nativeBuildInputs = [
      flatbuffers
      pkgs.python3
    ];
    buildInputs = [
      flatbuffers
      nlohmann_json
      cppcodec
    ];
  }
  ''
    contract=${lib.escapeShellArg "${contract}"}
    src=${lib.escapeShellArg "${src}"}
    entry=${lib.escapeShellArg entry}
    test -f "$src/$entry"
    # Schema metadata stays in the build directory and is consumed by Python.
    python3 ${./generate.py} ${../cpp}/card_cpp.py "$contract/_contract.json" \
      generated ${flatbuffers.src}/reflection/reflection.fbs > card_adapter.cc

    ${
      if isDarwin then
        ''
          exports=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
        ''
      else
        ''
          printf '{ global: tsugumi_main; local: *; };\n' > exports.map
          exports=(-Wl,--version-script,exports.map -Wl,-z,defs -ldl)
        ''
    }

    mkdir -p "$out/lib"
    $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
      -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
      -shared "''${exports[@]}" \
      -isystem generated/include -I${./.} -I${runtime}/include -I${../cpp/src} -I${legacyHeaders} \
      -isystem ${julia}/include/julia \
      -DTSUGUMI_LIBJULIA='"${juliaLib}"' \
      -DTSUGUMI_JULIA_PROJECT='"${project}"' \
      -DTSUGUMI_JULIA_DEPOT='"${depot}"' \
      -DTSUGUMI_JULIA_SDK='"${./tsugumi.jl}"' \
      -DTSUGUMI_JULIA_SOURCE=${lib.escapeShellArg (builtins.toJSON "${src}")} \
      -DTSUGUMI_JULIA_ENTRY=${lib.escapeShellArg (builtins.toJSON entry)} \
      -DTSUGUMI_JULIA_SOURCE_PREFIX=${lib.escapeShellArg (builtins.toJSON sourcePrefix)} \
      card_adapter.cc ${./bridge.cc} ${runtime}/lib/libtsugumi_abi9.a \
      -L${julia}/lib -Wl,-rpath,${julia}/lib -ljulia -pthread -o "$out/lib/main.so"
  ''
