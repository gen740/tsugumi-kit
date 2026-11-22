package tsugumi;

import java.util.Objects;

/** A structured failure to return to the host. */
public final class Failure extends RuntimeException {
    private final FailureKind kind;
    private final String cell;

    public Failure(FailureKind kind, String message) {
        this(kind, message, null);
    }

    public Failure(FailureKind kind, String message, String cell) {
        super(Objects.requireNonNull(message));
        this.kind = Objects.requireNonNull(kind);
        this.cell = cell;
    }

    public FailureKind kind() {
        return kind;
    }

    public String cell() {
        return cell;
    }
}
