{ pkgs, runtime }:
let
  inherit (pkgs) lib;
  sdk = ../sdk/go;
  portCodegen = pkgs.callPackage ./port-codegen.nix { };
  nativeFlags = ''
    cxx_flags=(-std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
      -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough)
  '';
  mkGoCardSdk =
    {
      schemas,
      goMod,
      sourcePrefix ? "src",
    }:
    pkgs.runCommandCC "tsugumi-go-sdk"
      {
        nativeBuildInputs = [
          pkgs.go
          pkgs.flatbuffers
          (pkgs.python3.withPackages (_: [ portCodegen ]))
        ];
        buildInputs = [ pkgs.flatbuffers ];
        env = {
          GOTOOLCHAIN = "local";
          GOWORK = "off";
          GOPROXY = "off";
        };
      }
      ''
        mkdir -p "$out/flatbuffers" "$out/lib" cpp/include module
        cp ${sdk}/{go.mod,runtime.go,display.go,native.go,host.h} "$out/"
        for file in ${pkgs.flatbuffers.src}/go/*.go; do
          case "$file" in *_test.go|*/doc.go) continue;; esac
          cp "$file" "$out/flatbuffers/"
        done

        schemas=${lib.escapeShellArg "${schemas}"}
        abi=${../abi}
        flatc --go --gen-object-api --go-import tsugumi/flatbuffers --go-module-name tsugumi \
          -o "$out" "$abi/card_enums.fbs" "$abi/card.fbs"
        flatc --go --gen-object-api --go-import tsugumi/flatbuffers --go-module-name tsugumi \
          -o "$out" "$schemas"/_{inputs,outputs,params}.fbs
        python3 ${sdk}/card_go.py "$schemas" "$out" \
          ${pkgs.flatbuffers.src}/reflection/reflection.fbs cpp ${../sdk/cpp}
        gofmt -w "$out/card.go"

        cp ${lib.escapeShellArg "${goMod}"} module/go.mod
        export GOCACHE="$TMPDIR/go-cache" GOMODCACHE="$TMPDIR/go-modules"
        module=$(go list -C module -m -f '{{.Path}}')
        python3 - "$module" ${lib.escapeShellArg sourcePrefix} "$out" <<'CODE'
        import json
        import sys
        from pathlib import Path

        module, prefix = map(json.dumps, sys.argv[1:3])
        (Path(sys.argv[3]) / "card_path.go").write_text(
            f"package tsugumi\nconst cardModule = {module}\nconst sourcePrefix = {prefix}\n"
        )
        CODE

        ${nativeFlags}
        mv "$out/card_ports.cc" .
        for source in ${sdk}/{host,entry}.cc card_ports.cc; do
          $CXX "''${cxx_flags[@]}" -Icpp/include -I${sdk} -I${runtime}/include \
            -I${../sdk/cpp/src} -c "$source"
        done
        $AR rcs "$out/lib/go-host.a" host.o card_ports.o entry.o
        cp ${runtime}/lib/libtsugumi_abi.a "$out/lib/"
      '';
  mkGoCard =
    {
      src,
      schemas,
      name ? "tsugumi-go-card",
      sourcePrefix ? "src",
      lock ? null,
      hash ? null,
      buildInputs ? [ ],
      nativeBuildInputs ? [ ],
      tools ? [ ],
    }:
    assert lib.assertMsg (
      (lock == null) == (hash == null)
    ) "goCard: supply lock (go.sum) and hash together for external Go modules";
    let
      generatedSdk = mkGoCardSdk {
        inherit schemas sourcePrefix;
        goMod = "${src}/go.mod";
      };
      inherit (pkgs.stdenv.hostPlatform) isDarwin;
      checkLock = lib.optionalString (lock != null) ''
        cmp -s go.sum ${lock} || {
          echo "goCard: go.sum changed; update the Card lock and hash" >&2
          exit 1
        }
      '';
      proxy =
        if lock == null then
          "off"
        else
          "file://${
            (pkgs.buildGoModule {
              name = "${name}-deps-${
                builtins.substring 0 16 (
                  builtins.hashString "sha256" (
                    builtins.hashFile "sha256" "${src}/go.mod" + builtins.hashFile "sha256" lock
                  )
                )
              }";
              src = pkgs.runCommand "tsugumi-go-lock" { } ''
                mkdir -p "$out/tsugumi"
                cp ${src}/go.mod "$out/go.mod"
                cp ${lock} "$out/go.sum"
                cp ${sdk}/go.mod "$out/tsugumi/go.mod"
              '';
              vendorHash = hash;
              proxyVendor = true;
              modPostBuild = checkLock;
            }).goModules
          }";
    in
    pkgs.runCommandCC name
      {
        nativeBuildInputs = [
          pkgs.go
        ]
        ++ nativeBuildInputs;
        buildInputs = [ pkgs.flatbuffers ] ++ buildInputs;
        env = {
          inherit (pkgs.go) GOOS GOARCH;
          CGO_ENABLED = "1";
          GOTOOLCHAIN = "local";
          GOWORK = "off";
          GOSUMDB = "off";
          GOPROXY = proxy;
        };
      }
      ''
        api="$out/share/tsugumi/api"
        mkdir -p card "$out/lib" "$api"
        cp ${generatedSdk}/{README.md,card.go} "$api/"
        for package in ${generatedSdk}/*/; do
          case "$(basename "$package")" in flatbuffers|lib|Tsugumi) continue;; esac
          cp -r "$package" "$api/"
        done
        cp -r --no-preserve=mode ${src}/. card/
        cd card
        if [ -e tsugumi_adapter.go ]; then
          echo "goCard: tsugumi_adapter.go is reserved for the generated SDK" >&2
          exit 1
        fi
        # Replace any development copy or symlink with the SDK for these schemas.
        rm -rf tsugumi
        ln -s ${generatedSdk} tsugumi
        cp ${sdk}/adapter.go.in tsugumi_adapter.go
        ${lib.optionalString (lock != null) "install -m 0644 ${lock} go.sum"}
        export GOCACHE="$TMPDIR/go-cache" GOMODCACHE="$TMPDIR/go-modules"

        ${
          if isDarwin then
            ''
              payload_link="-Wl,-u,_tsugumi_go_entry -Wl,-exported_symbol,_tsugumi_go_entry -Wl,-undefined,error"
              loader_link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_go_entry; local: *; };\n' > "$TMPDIR/payload.exports"
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/loader.exports"
              payload_link="-Wl,-u,tsugumi_go_entry -Wl,--version-script,$TMPDIR/payload.exports -Wl,-z,defs -Wl,-z,nodelete"
              loader_link=(-Wl,--version-script,"$TMPDIR/loader.exports" -Wl,-z,defs -ldl)
            ''
        }

        # Let Go carry native linker flags from every cgo package to the shared library.
        # The loader looks up the entry dynamically, so -u pulls it from the
        # SDK's archive even though no Go code references it directly.
        go build -mod=readonly -trimpath -buildmode=c-shared \
          -ldflags="-w -buildid= -linkmode=external -extldflags '$payload_link'" \
          -o "$TMPDIR/go-card.so" .
        install -m 755 "$TMPDIR/go-card.so" "$out/lib/go-card.so"
        ${checkLock}

        ${nativeFlags}
        ${import ./prepare-tools.nix { inherit lib; } tools}
        $CXX "''${cxx_flags[@]}" -shared "''${toolPathObject[@]}" -I${runtime}/include -I${../sdk/cpp/src} \
          ${sdk}/loader.cc ${runtime}/lib/libtsugumi_abi.a -pthread "''${loader_link[@]}" \
          -o "$out/lib/main.so"
      '';
in
{
  goCard = mkGoCard;
  goCardSdk = mkGoCardSdk;
}
