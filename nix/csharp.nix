{
  perSystem =
    { pkgs, kit, ... }:
    let
      note =
        (import ../tests/card-eval.nix { inherit pkgs; }).call ../templates/csharp-write-note/note
          { };
    in
    {
      checks.csharpAbi = pkgs.callPackage ../tests/csharp/check.nix { inherit kit; };
      checks.csharpWriteNoteTemplate = pkgs.runCommand "tsugumi-csharp-note-template-check" { } ''
        test -s ${note.run}/lib/main.so
        test -s ${note.api}/share/tsugumi/api/Card.g.cs
        touch "$out"
      '';
    };
}
