package tsugumi;

/** Cancellation and deadline effects for the current invocation. */
public final class Effects {
    private Effects() {}

    /** True if the host has cancelled the invocation or no call is active. */
    public static native boolean cancelled();

    /** Remaining milliseconds, -1 for no deadline, or 0 between calls. */
    public static native long remainingMs();

    static native byte[] recv(byte[] view, long timeoutMs, int[] status);
    static native byte[] artifactDir(byte[] id);
    static native byte[] artifactUpload(byte[] path);
    static native byte[] artifactOpen(byte[] id);
    static native void send(byte[] view, byte[] bytes);
}
