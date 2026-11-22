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
  ipaexfont,
  runtime,
}:
{
  src,
  contract,
  name ? "tsugumi-r-card",
  entry ? "main.R",
  packages ? (_: [ ]),
  fonts ? [
    dejavu_fonts
    ipaexfont
  ],
}:
let
  legacyHeaders = lib.fileset.toSource {
    root = ../.;
    fileset = ../display.hh;
  };
  generated =
    runCommand "tsugumi-r-card-codecs"
      {
        nativeBuildInputs = [
          flatbuffers
          python3
        ];
      }
      ''
        contract=${lib.escapeShellArg "${contract}"}
        mkdir -p "$out/include"
        flatc --cpp --gen-object-api --scoped-enums --force-empty --force-empty-vectors \
          -o "$out/include" "$contract"/*.fbs
        for schema in _inputs _outputs _params; do
          flatc --binary --schema "$contract/$schema.fbs"
          flatc --json --strict-json "${flatbuffers.src}/reflection/reflection.fbs" -- "$schema.bfbs"
        done
        python3 ${../cpp/card_cpp.py} "$contract/_contract.json" "$out"
        python3 ${./codegen.py} ${../cpp/card_cpp.py} "$contract/_contract.json" \
          _inputs.json _outputs.json _params.json > "$out/src/r_adapter.cc"
        rm "$out/src/card_adapter.cc"
      '';
  rHome = "${R}/lib/R";
  rShared = if stdenv.hostPlatform.isDarwin then "libR.dylib" else "libR.so";
in
runCommandCC name
  {
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
    mkdir -p "$out/lib"
    source_root=${lib.escapeShellArg "${src}"}
    source="$source_root"/${lib.escapeShellArg entry}
    test -f "$source"
    includes=(-I"${runtime}/include" -I"${../cpp/src}" -isystem "${generated}/include" -I"${./.}" -I"${legacyHeaders}")
    $CC -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -pthread \
      -I"${runtime}/include" -I"${rHome}/include" -DTSUGUMI_LIBR='"${rHome}/lib/${rShared}"' \
      -DTSUGUMI_R_HOME='"${rHome}"' -DTSUGUMI_R_LIBS="\"$R_LIBS_SITE\"" \
      -DTSUGUMI_R_SDK='"${./tsugumi.R}"' -DTSUGUMI_R_SOURCE="\"$source\"" \
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
    $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
      -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
      -shared "''${link[@]}" "''${includes[@]}" \
      "${generated}/src/r_adapter.cc" ${./bridge.cc} embed.o \
      "${runtime}/lib/libtsugumi_abi9.a" -pthread \
      -L"${rHome}/lib" -Wl,-rpath,"${rHome}/lib" -lR -o "$out/lib/main.so"
  ''
