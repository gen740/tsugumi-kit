package tsugumi;

/** Cancellation and deadline effects for the current invocation. */
public final class Effects {
    private Effects() {}

    /** True if the host has cancelled the invocation or no call is active. */
    public static native boolean cancelled();

    /** Remaining milliseconds, -1 for no deadline, or 0 between calls. */
    public static native long remainingMs();

    static native void viewOpen(byte[] view, int mode, long width, long height,
                                byte[] route, byte[] session);
    static native byte[] routeRegister(byte[] session);
    static native void routeUnregister(byte[] route);
    static native byte[] recv(long timeoutMs, int[] status, byte[] route, byte[] session);
    static native byte[] artifactDir(byte[] id, boolean keep);
    static native byte[] artifactUpload(byte[] path, boolean keep);
    static native byte[] artifactOpen(byte[] id);
    static native void send(byte[] session, byte[] bytes, boolean framed);
}
