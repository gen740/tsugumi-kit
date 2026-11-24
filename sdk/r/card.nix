{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  nlohmann_json,
  python3,
  R,
  rPackages,
  rWrapper,
  makeFontsConf,
  dejavu_fonts,
  ipaexGothic,
  runtime,
  portCodegen,
  cardInterface,
  paramValues,
  publishApi,
}:
let
  generate =
    {
      inputs,
      outputs,
      params ? { },
    }:
    let
      interface = cardInterface { inherit inputs outputs params; };
    in
    runCommand "tsugumi-r-card-glue"
      {
        nativeBuildInputs = [
          flatbuffers
          (python3.withPackages (_: [ portCodegen ]))
        ];
      }
      ''
        schemas=${lib.escapeShellArg "${interface}"}
        python3 ${./codegen.py} ${../cpp} ${./inputs.R} "$schemas" "$out" \
          ${flatbuffers.src}/reflection/reflection.fbs
      '';
  rCard =
    {
      src,
      inputs,
      outputs,
      params ? { },
      name ? "tsugumi-r-card",
      entry ? "main.R",
      packages ? (_: [ ]),
      tools ? [ ],
      fonts ? [
        dejavu_fonts
        ipaexGothic
      ],
    }:
    let
      legacyHeaders = lib.fileset.toSource {
        root = ../.;
        fileset = ../display.hh;
      };
      # The C++ glue, its object API headers and the Card's API, from the schemas.
      generated = generate { inherit inputs outputs params; };
      rHome = "${R}/lib/R";
      rShared = if stdenv.hostPlatform.isDarwin then "libR.dylib" else "libR.so";
    in
    runCommandCC name
      {
        passthru = { inherit generated; };
        nativeBuildInputs = [ python3 ];
        buildInputs = [
          flatbuffers
          nlohmann_json
          R
          rPackages.jsonlite
        ]
        ++ rWrapper.recommendedPackages
        ++ packages rPackages;
      }
      ''
        mkdir -p "$out/lib" "$out/share/tsugumi"
        python3 ${./codegen.py} --params ${paramValues params} "$out/share/tsugumi/params.R"
        source_root=${lib.escapeShellArg "${src}"}
        source="$source_root"/${lib.escapeShellArg entry}
        test -f "$source"
        includes=(-I"${runtime}/include" -I"${../cpp/src}" -isystem "${generated}/include" -I"${./.}" -I"${legacyHeaders}")
        $CC -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -pthread \
          -I"${runtime}/include" -I"${rHome}/include" -DTSUGUMI_LIBR='"${rHome}/lib/${rShared}"' \
          -DTSUGUMI_R_HOME='"${rHome}"' -DTSUGUMI_R_LIBS="\"$R_LIBS_SITE\"" \
          -DTSUGUMI_R_SDK='"${./tsugumi.R}"' -DTSUGUMI_R_SOURCE="\"$source\"" \
          -DTSUGUMI_R_PARAMS="\"$out/share/tsugumi/params.R\"" \
          -DTSUGUMI_R_CARD_API="\"${generated}/api/card.R\"" \
          -DTSUGUMI_R_SOURCE_ROOT="\"$source_root\"" \
          -DTSUGUMI_R_FONTS='"${makeFontsConf { fontDirectories = fonts; }}"' \
          -c ${./embed.c} -o embed.o
        ${
          if stdenv.hostPlatform.isDarwin then
            ''
              link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/exports"
              link=(-Wl,--version-script,"$TMPDIR/exports" -Wl,-z,defs -ldl)
            ''
        }
        ${import ../../nix/prepare-tools.nix { inherit lib; } tools}
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
          -shared "''${toolPathObject[@]}" "''${link[@]}" "''${includes[@]}" \
          "${generated}/src/r_adapter.cc" ${./bridge.cc} embed.o \
          "${runtime}/lib/libtsugumi_abi.a" -pthread \
          -L"${rHome}/lib" -Wl,-rpath,"${rHome}/lib" -lR -o "$out/lib/main.so"
      '';
  rApi =
    args:
    let
      generated = generate args;
    in
    publishApi "r" ''
      cp -r "${generated}/api/." "$api/"
    '';
in
{
  inherit rCard rApi;
}
