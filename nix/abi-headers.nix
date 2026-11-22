{ runCommand, flatbuffers }:
runCommand "tsugumi-abi9-headers" { nativeBuildInputs = [ flatbuffers ]; } ''
  abi=${../abi/9}
  mkdir -p "$out/include/tsugumi/abi9" "$out/include/tsugumi/protocol"
  cp "$abi/card_abi.h" "$out/include/tsugumi/abi9/card_abi.h"
  flatc --cpp --gen-object-api --scoped-enums -o "$out/include/tsugumi/protocol" \
    "$abi/card_enums.fbs" "$abi/card.fbs"
''
