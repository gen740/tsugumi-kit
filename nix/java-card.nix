{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  jdk21_headless,
  python3,
  runtime,
}:
let
  jdk = jdk21_headless;
  inherit (stdenv.hostPlatform) isDarwin;
  jvmDir = if isDarwin then "${jdk}/lib/server" else "${jdk}/lib/openjdk/lib/server";
  # Compile the official Java runtime from the same source as flatc. Its
  # generated classes reference a version-specific runtime constant.
  flatbuffersJava =
    runCommand "flatbuffers-java-${flatbuffers.version}"
      {
        nativeBuildInputs = [ jdk ];
      }
      ''
        mkdir classes "$out"
        find ${flatbuffers.src}/java/src/main/java -name '*.java' -type f -print |
          LC_ALL=C sort > sources
        javac -encoding UTF-8 -d classes @sources
        jar --create --date=1980-01-01T00:00:02Z --file "$out/flatbuffers-java.jar" -C classes .
      '';
in
{
  src,
  contract,
  jars ? [ ],
  mainClass ? "card.Main",
  tools ? [ ],
}:
let
  classPath = lib.concatStringsSep ":" (
    [ "${flatbuffersJava}/flatbuffers-java.jar" ] ++ map toString jars
  );
in
runCommandCC "tsugumi-java-card"
  {
    nativeBuildInputs = [
      flatbuffers
      jdk
      python3
    ];
    buildInputs = [ flatbuffers ];
    TSUGUMI_JAVA_CLASSPATH = classPath;
    TSUGUMI_JAVA_MAIN_CLASS = mainClass;
    strictDeps = true;
  }
  ''
    src=${lib.escapeShellArg "${src}"}
    mkdir -p generated classes "$out/lib"
    python3 ${../sdk/java/generate.py} ${../sdk/cpp} ${lib.escapeShellArg "${contract}"} \
      "$src" "$TSUGUMI_JAVA_MAIN_CLASS" generated ${flatbuffers.src}/reflection/reflection.fbs \
      ${../abi/9}/tsugumi_enums.fbs
    flatc --java --java-package-prefix tsugumi.wire -o generated/java \
      ${../abi/9}/{tsugumi_enums,card}.fbs
    # Use a NUL-delimited list so source paths may contain whitespace.
    find "$src" -type f -name '*.java' ! -name '._*' -print0 |
      LC_ALL=C sort -z > card-sources
    test -s card-sources || { echo "javaCard: no .java sources in src" >&2; exit 1; }
    find ${../sdk/java/src} generated/java -type f -name '*.java' -print0 > sdk-sources
    mapfile -d "" -t sources < sdk-sources
    mapfile -d "" -t card_sources < card-sources
    javac -encoding UTF-8 -proc:none -classpath "$TSUGUMI_JAVA_CLASSPATH" -d classes \
      "''${sources[@]}" "''${card_sources[@]}"
    jar --create --date=1980-01-01T00:00:02Z --file "$out/lib/card.jar" -C classes .

    ${
      if isDarwin then
        ''
          link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
        ''
      else
        ''
          printf '{ global: tsugumi_main; local: *; };\n' > exports
          link=(-Wl,--version-script,"$PWD/exports" -Wl,-z,defs)
        ''
    }
    $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden \
      -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough -shared "''${link[@]}" \
      -Igenerated/include -I${../sdk/java} -I${../sdk/cpp/src} -I${runtime}/include \
      -I${jdk}/include -I${jdk}/include/${if isDarwin then "darwin" else "linux"} \
      "-DTSUGUMI_JAVA_CLASSPATH=\"$out/lib/card.jar:$TSUGUMI_JAVA_CLASSPATH\"" \
      generated/card_adapter.cc ${../sdk/java/host.cc} ${runtime}/lib/libtsugumi_abi9.a \
      -Wl,-rpath,${jvmDir} "${jvmDir}/libjvm${stdenv.hostPlatform.extensions.sharedLibrary}" \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o "$out/lib/main.so"
    ${import ./install-tools.nix { inherit lib; } tools}
  ''
