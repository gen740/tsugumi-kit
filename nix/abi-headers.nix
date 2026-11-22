{ runCommand, flatbuffers }:
runCommand "tsugumi-abi-headers" { nativeBuildInputs = [ flatbuffers ]; } ''
  abi=${../abi}
  mkdir -p "$out/include/tsugumi/abi" "$out/include/tsugumi/protocol"
  cp "$abi/card_abi.h" "$out/include/tsugumi/abi/card_abi.h"
  flatc --cpp --gen-object-api --scoped-enums -o "$out/include/tsugumi/protocol" \
    "$abi/card_enums.fbs" "$abi/card.fbs"
''
