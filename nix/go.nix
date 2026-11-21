{
  config,
  lib,
  self,
  withSystem,
  ...
}:
let
  mkGoCard =
    pkgs: runtime:
    {
      src,
      contract,
      name ? "tsugumi-go-card",
      sourcePrefix ? "src",
      lock ? null,
      hash ? null,
      buildInputs ? [ ],
      nativeBuildInputs ? [ ],
    }:
    assert lib.assertMsg (
      (lock == null) == (hash == null)
    ) "goCard: supply lock (go.sum) and hash together for external Go modules";
    let
      sdk = ../sdk/go;
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
          pkgs.flatbuffers
          pkgs.python3
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
        mkdir -p card "$out/lib"
        cp -r --no-preserve=mode ${src}/. card/
        cd card
        if [ -e tsugumi ] || [ -e tsugumi_adapter.go ]; then
          echo "goCard: tsugumi/ and tsugumi_adapter.go are reserved for the generated SDK" >&2
          exit 1
        fi
        ${lib.optionalString (lock != null) "install -m 0644 ${lock} go.sum"}
        mkdir -p tsugumi/flatbuffers "$TMPDIR/include"
        cp ${sdk}/{go.mod,runtime.go,display.go,host.h} tsugumi/
        cp ${sdk}/adapter.go.in tsugumi_adapter.go
        for file in ${pkgs.flatbuffers.src}/go/*.go; do
          case "$file" in *_test.go|*/doc.go) continue;; esac
          cp "$file" tsugumi/flatbuffers/
        done
        contract=${lib.escapeShellArg "${contract}"}
        abi=${../abi/9}
        flatc --go --gen-object-api --go-import tsugumi/flatbuffers --go-module-name tsugumi \
          -o tsugumi "$abi/tsugumi_enums.fbs" "$abi/card.fbs"
        for schema in _inputs _outputs _params; do
          flatc --go --gen-object-api --go-import tsugumi/flatbuffers --go-module-name tsugumi \
            -o tsugumi "$contract/$schema.fbs"
          flatc --cpp --scoped-enums -o "$TMPDIR/include" "$contract/$schema.fbs"
        done
        python3 ${sdk}/card_go.py "$contract/_contract.json" tsugumi
        export GOCACHE="$TMPDIR/go-cache" GOMODCACHE="$TMPDIR/go-modules"
        python3 - "$(go list -m -f '{{.Path}}')" ${lib.escapeShellArg sourcePrefix} <<'PY'
        import json
        import sys
        from pathlib import Path

        module, prefix = map(json.dumps, sys.argv[1:])
        Path("tsugumi/card_path.go").write_text(
            f"package tsugumi\nconst cardModule = {module}\nconst sourcePrefix = {prefix}\n"
        )
        PY
        # The bridge is linked outside cgo against the shared static ABI runtime.
        mv tsugumi/card_ports.cc "$TMPDIR/"
        go build -mod=readonly -trimpath -buildmode=c-archive -ldflags="-s -w -buildid=" -o "$TMPDIR/card.a" .
        ${checkLock}

        ${
          if pkgs.stdenv.hostPlatform.isDarwin then
            ''
              link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/exports"
              link=(-Wl,--version-script,"$TMPDIR/exports" -Wl,-z,defs -Wl,-z,nodelete -ldl -lm)
            ''
        }
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough -shared \
          -I"$TMPDIR/include" -I${sdk} -I${runtime}/include -I${../sdk/cpp/src} \
          ${sdk}/host.cc "$TMPDIR/card_ports.cc" "$TMPDIR/card.a" \
          ${runtime}/lib/libtsugumi_abi9.a -pthread "''${link[@]}" -o "$out/lib/main.so"
      '';
in
{
  # flake-parts leaves custom outputs unique unless their merge type is declared.
  options.flake.lib = lib.mkOption {
    type = lib.types.lazyAttrsOf (lib.types.lazyAttrsOf lib.types.raw);
  };

  config.flake.lib = lib.genAttrs config.systems (
    system:
    withSystem system ({ pkgs, config, ... }: { goCard = mkGoCard pkgs config.packages.cppRuntime; })
  );

  config.perSystem =
    {
      pkgs,
      config,
      system,
      ...
    }:
    {
      checks.goAbi9 = pkgs.callPackage ../tests/go/check.nix {
        goCard = self.lib.${system}.goCard;
        runtime = config.packages.cppRuntime;
      };
    };
}
