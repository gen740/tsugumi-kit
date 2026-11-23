{ pkgs, kit }:
let
  schema = pkgs.writeText "_outputs.fbs" ''
    include "tsugumi/artifact.fbs";
    namespace example;
    table Nested { file:tsugumi.Artifact (required); }
    union Member { File:tsugumi.Artifact }
    table Listing {
      nested:Nested (required);
      files:[tsugumi.Artifact] (required);
      member:Member;
    }
    table Outputs {
      listing:Listing (required);
      unused:Nested;
    }
    root_type Outputs;
  '';
  file = pkgs.writeText "payload.txt" "file payload";
  folder = pkgs.runCommand "payload-folder" { } ''
    mkdir "$out"
    echo folder payload > "$out/contents.txt"
  '';
  run = kit.input.input {
    outputs = schema;
    values.listing = {
      nested.file = kit.input.artifact file;
      files = [
        (kit.input.artifact folder)
        (kit.input.artifact file)
      ];
      member_type = "File";
      member = kit.input.artifact folder;
    };
  };
  omittedRequired = kit.input.input {
    outputs = schema;
    values = { };
  };
in
pkgs.runCommand "tsugumi-input-contract-check" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  python3 - "${run}/share/tsugumi/input" "${file}" "${folder}" <<'PY'
  import hashlib
  import sys
  from pathlib import Path

  root, file, folder = map(Path, sys.argv[1:])
  def artifact_id(source):
      return hashlib.sha256(b"listing\0" + str(source).encode()).hexdigest()[:32]

  file_id = artifact_id(file)
  folder_id = artifact_id(folder)
  assert (root / "ports/listing.artifacts").read_text().splitlines() == sorted((file_id, folder_id))
  assert not (root / "ports/unused").exists()
  assert (root / "artifacts" / file_id / "payload.txt").read_text() == "file payload"
  assert (root / "artifacts" / folder_id / "contents.txt").read_text() == "folder payload\n"
  wire = (root / "ports/listing").read_bytes()
  assert wire.count(bytes.fromhex(file_id)) == 2
  assert wire.count(bytes.fromhex(folder_id)) == 2
  PY
  test -d "${omittedRequired}/share/tsugumi/input/ports"
  test ! -e "${omittedRequired}/share/tsugumi/input/ports/listing"
  touch "$out"
''
