{
  runCommand,
  flatbuffers,
  python3,
}:
runCommand "tsugumi-abi-headers"
  {
    nativeBuildInputs = [
      flatbuffers
      python3
    ];
  }
  ''
    abi=${../abi}
    mkdir -p "$out/include/tsugumi/abi" "$out/include/tsugumi/protocol"
    cp "$abi/card_abi.h" "$out/include/tsugumi/abi/card_abi.h"
    cp "$abi/artifact.fbs" "$out/include/tsugumi/artifact.fbs"
    flatc --cpp --gen-object-api --scoped-enums -o "$out/include/tsugumi/protocol" \
      "$abi/artifact.fbs" "$abi/card_enums.fbs" "$abi/card.fbs"
    # Extend the native object used for this fixed ABI type in every schema.
    python3 - "$out/include/tsugumi/protocol/artifact_generated.h" <<'PYTHON'
    import pathlib
    import sys

    path = pathlib.Path(sys.argv[1])
    text = path.read_text()
    marker = "  typedef Artifact TableType;"
    assert text.count(marker) == 1
    path.write_text(text.replace(marker, marker + """
      static auto dir() -> std::pair<ArtifactT, std::filesystem::path>;
      static auto upload(const std::filesystem::path& source) -> ArtifactT;
      auto open() const -> std::filesystem::path;""")
        .replace('#include "flatbuffers/flatbuffers.h"', '#include <filesystem>\n#include "flatbuffers/flatbuffers.h"'))
    PYTHON
  ''
