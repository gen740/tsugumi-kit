package tsugumi;

import java.nio.file.Path;
import static java.nio.charset.StandardCharsets.UTF_8;

/**
 * Filesystem access for the Artifact IDs in Card data. Knowing an ID is enough
 * to open its sealed Artifact.
 */
public final class Artifacts {
    private Artifacts() {}

    public record Directory(ArtifactT artifact, Path path) {}

    /** A new kept Artifact and its writable directory. */
    public static Directory dir() {
        return dir(true);
    }

    /**
     * A new Artifact and its writable directory. Without keep, the Artifact
     * exists only while its run runs.
     */
    public static Directory dir(boolean keep) {
        var id = new byte[16];
        var path = Effects.artifactDir(id, keep);
        return new Directory(create(id), Path.of(new String(path, UTF_8)));
    }

    /** A new kept Artifact holding a copy of the file or directory. */
    public static ArtifactT upload(Path source) {
        return upload(source, true);
    }

    /** A new Artifact holding a copy; without keep it exists only while its run runs. */
    public static ArtifactT upload(Path source, boolean keep) {
        return create(Effects.artifactUpload(source.toString().getBytes(UTF_8), keep));
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
