package tsugumi;

import java.nio.file.Path;
import static java.nio.charset.StandardCharsets.UTF_8;

/** Filesystem access for capability IDs embedded in Card data. */
public final class Artifacts {
    private Artifacts() {}

    public record Directory(ArtifactT artifact, Path path) {}

    public static Directory dir() {
        var id = new byte[16];
        var path = Effects.artifactDir(id);
        return new Directory(create(id), Path.of(new String(path, UTF_8)));
    }

    public static ArtifactT upload(Path source) {
        return create(Effects.artifactUpload(source.toString().getBytes(UTF_8)));
    }

    static Path open(ArtifactT artifact) {
        if (artifact.getId() == null) {
            throw new IllegalStateException("Artifact is missing an ID");
        }
        var id = new byte[16];
        int[] values = artifact.getId().getBytes();
        for (int index = 0; index < id.length; index++) id[index] = (byte) values[index];
        return Path.of(new String(Effects.artifactOpen(id), UTF_8));
    }

    private static ArtifactT create(byte[] id) {
        var artifact = new ArtifactT();
        var values = artifact.getId().getBytes();
        for (int index = 0; index < id.length; index++) values[index] = Byte.toUnsignedInt(id[index]);
        return artifact;
    }
}
