#nullable enable
using System;

namespace tsugumi;

public partial class ArtifactT
{
    // Without keep, the Artifact exists only while its run runs.
    public static (ArtifactT Artifact, string Path) Dir(bool keep = true)
    {
        var artifact = new ArtifactT { Id = new ArtifactIdT() };
        return (artifact, global::Tsugumi.Effects.ArtifactDir(artifact.Id.Bytes, keep));
    }

    public static ArtifactT Upload(string source, bool keep = true)
    {
        var artifact = new ArtifactT { Id = new ArtifactIdT() };
        global::Tsugumi.Effects.ArtifactUpload(source, artifact.Id.Bytes, keep);
        return artifact;
    }

    public string Open()
    {
        if (Id?.Bytes is not { Length: 16 } id)
            throw new InvalidOperationException("Artifact is missing an ID");
        return global::Tsugumi.Effects.ArtifactOpen(id);
    }
}
