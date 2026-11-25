package tsugumi;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
import java.util.Objects;
import java.util.StringJoiner;

import static java.nio.charset.StandardCharsets.UTF_8;

/** Live messages for bundled or custom Card views. */
public final class Display {
    private Display() {}

    public record Text(String text) {}
    public record Markdown(String text) {}
    public record Table(List<String> columns, List<? extends List<?>> rows) {}
    public record Image(byte[] bytes, String mediaType, String alt) {
        public Image(byte[] bytes) {
            this(bytes, "image/png", "");
        }
    }
    public record LineSeries(String id, String label, double[] x, double[] y) {}
    public record LineChart(List<LineSeries> series) {}
    public record Points(float[] x, float[] y, float[] z, int[] colors) {
        public Points(float[] x, float[] y, float[] z) {
            this(x, y, z, new int[0]);
        }
    }

    public enum ViewMode { PANEL, WINDOW, FULLSCREEN }
    public record OpenedView(byte[] route, byte[] session) {}

    public enum ReceiveStatus { MESSAGE, CLOSE, TIMEOUT, CANCELLED }
    public record Received(ReceiveStatus status, byte[] bytes, byte[] route, byte[] session) {}

    public static final long HIDE_CURSOR = 1;
    public static final long LOCK_POINTER = 2;

    public static OpenedView viewOpen(String view) {
        return viewOpen(view, ViewMode.PANEL, 0, 0, 0);
    }

    /** Open a declared view, with an optional initial size for WINDOW. */
    public static OpenedView viewOpen(String view, ViewMode mode, long flags, long width, long height) {
        if (view.indexOf(0) >= 0) {
            throw new IllegalArgumentException("View names cannot contain a NUL byte");
        }
        if (width < 0 || width > 0xffffffffL || height < 0 || height > 0xffffffffL) {
            throw new IllegalArgumentException("Expected unsigned 32-bit dimensions");
        }
        if (flags < 0 || flags > 0xffffffffL) {
            throw new IllegalArgumentException("Expected unsigned 32-bit view flags");
        }
        var route = new byte[16];
        var session = new byte[16];
        Effects.viewOpen(view.getBytes(UTF_8), mode.ordinal(), flags, width, height, route, session);
        return new OpenedView(route, session);
    }

    private static byte[] requireId(byte[] id) {
        if (Objects.requireNonNull(id).length != 16) {
            throw new IllegalArgumentException("Expected a 16-byte view ID");
        }
        return id;
    }

    public static void viewClose(byte[] session) {
        Effects.viewClose(requireId(session));
    }

    public static final class WindowClosed extends IllegalStateException {
        public WindowClosed(String message) {
            super(message);
        }
    }

    public static byte[] routeRegister(byte[] session) {
        return Effects.routeRegister(requireId(session));
    }

    public static void routeUnregister(byte[] route) {
        Effects.routeUnregister(requireId(route));
    }

    /** Wait for an event addressed to any route registered by this process. */
    public static Received recv(long timeoutMs) {
        var status = new int[1];
        var route = new byte[16];
        var session = new byte[16];
        byte[] bytes = Effects.recv(timeoutMs, status, route, session);
        var kind = ReceiveStatus.values()[status[0]];
        var event = kind == ReceiveStatus.MESSAGE || kind == ReceiveStatus.CLOSE;
        return new Received(kind, bytes, event ? route : null, event ? session : null);
    }

    /** Send the given payload directly to a window, including between calls. */
    public static void send(byte[] session, byte[] bytes) {
        Effects.send(requireId(session), Objects.requireNonNull(bytes), false);
    }

    private static void sendFramed(byte[] session, byte[] bytes) {
        Effects.send(requireId(session), bytes, true);
    }

    private static void openAndSend(String view, byte[] bytes) {
        sendFramed(viewOpen(view).session(), bytes);
    }

    public static void send(String text) {
        send(new Text(text));
    }

    public static void send(Text value) {
        sendText("text", value.text());
    }

    public static void send(Markdown value) {
        sendText("markdown", value.text());
    }

    public static void send(Table value) {
        var rows = new StringJoiner(",", "[", "]");
        for (var row : value.rows()) {
            if (row.size() != value.columns().size()) {
                throw new IllegalArgumentException("Display table rows must match the column count");
            }
            rows.add(strings(row));
        }
        String config = "{\"columns\":" + strings(value.columns()) + ",\"rows\":" + rows + "}";
        openAndSend("table", config.getBytes(UTF_8));
    }

    public static void send(Image value) {
        String config = "{\"alt\":" + quote(value.alt())
                + ",\"media_type\":" + quote(value.mediaType()) + "}";
        var frame = frame(config, value.bytes().length);
        openAndSend("image", frame.put(value.bytes()).array());
    }

    public static void send(Points value) {
        send(value, null);
    }

    public static void send(Points value, byte[] session) {
        int count = value.x().length;
        int colors = value.colors().length;
        if (value.y().length != count || value.z().length != count || (colors != 0 && colors != count)) {
            throw new IllegalArgumentException("Points require matching x, y, z and optional RGB colors");
        }
        var frame = frame("{\"count\":" + count + "}", 12L * count + 4L * colors);
        for (float x : value.x()) frame.putFloat(x);
        for (float y : value.y()) frame.putFloat(y);
        for (float z : value.z()) frame.putFloat(z);
        for (int rgb : value.colors()) frame.putInt(rgb);
        sendFramed(session == null ? viewOpen("points").session() : session, frame.array());
    }

    public static void send(LineChart value) {
        send(value, null);
    }

    public static void send(LineChart value, byte[] session) {
        var items = new StringJoiner(",", "[", "]");
        long count = 0;
        for (var series : value.series()) {
            int size = series.x().length;
            if (series.y().length != size) {
                throw new IllegalArgumentException("Line chart axes require matching values");
            }
            items.add("{\"count\":" + size + ",\"id\":" + quote(series.id())
                    + ",\"label\":" + quote(series.label()) + ",\"offset\":" + count + "}");
            count += size;
        }
        var frame = frame("{\"series\":" + items + "}", Math.multiplyExact(count, 16));
        for (var series : value.series()) {
            for (double x : series.x()) putFinite(frame, x);
        }
        for (var series : value.series()) {
            for (double y : series.y()) putFinite(frame, y);
        }
        sendFramed(session == null ? viewOpen("line-chart").session() : session, frame.array());
    }

    private static void putFinite(ByteBuffer frame, double value) {
        if (!Double.isFinite(value)) {
            throw new IllegalArgumentException("Line chart axes require finite values");
        }
        frame.putDouble(value);
    }

    private static void sendText(String view, String value) {
        openAndSend(view, ("{\"text\":" + quote(value) + "}").getBytes(UTF_8));
    }

    private static String strings(List<?> values) {
        var items = new StringJoiner(",", "[", "]");
        for (var value : values) items.add(quote(String.valueOf(value)));
        return items.toString();
    }

    private static ByteBuffer frame(String config, long size) {
        byte[] metadata = config.getBytes(UTF_8);
        int capacity = Math.toIntExact(Math.addExact(size, Integer.BYTES + (long) metadata.length));
        return ByteBuffer.allocate(capacity).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(metadata.length).put(metadata);
    }

    // JSON metadata is private to the bundled HTML views.
    private static String quote(String value) {
        var quoted = new StringBuilder().append('"');
        for (int index = 0; index < value.length(); index++) {
            char ch = value.charAt(index);
            switch (ch) {
                case '"' -> quoted.append("\\\"");
                case '\\' -> quoted.append("\\\\");
                case '\b' -> quoted.append("\\b");
                case '\f' -> quoted.append("\\f");
                case '\n' -> quoted.append("\\n");
                case '\r' -> quoted.append("\\r");
                case '\t' -> quoted.append("\\t");
                default -> {
                    if (ch < 0x20 || Character.isSurrogate(ch)) {
                        quoted.append("\\u").append(java.util.HexFormat.of().toHexDigits(ch));
                    } else {
                        quoted.append(ch);
                    }
                }
            }
        }
        return quoted.append('"').toString();
    }
}
