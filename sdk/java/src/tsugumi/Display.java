package tsugumi;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
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

    public enum ReceiveStatus { MESSAGE, TIMEOUT, CANCELLED }
    public record Received(ReceiveStatus status, byte[] bytes) {}

    /** Wait for a message, timeout or cancellation, including between calls. */
    public static Received recv(String view, long timeoutMs) {
        if (view.indexOf(0) >= 0) {
            throw new IllegalArgumentException("View names cannot contain a NUL byte");
        }
        var status = new int[1];
        byte[] bytes = Effects.recv(view.getBytes(UTF_8), timeoutMs, status);
        return new Received(ReceiveStatus.values()[status[0]], bytes);
    }

    /** Send the given payload to a view, including from a background thread. */
    public static void send(String view, byte[] bytes) {
        if (view.indexOf(0) >= 0) {
            throw new IllegalArgumentException("View names cannot contain a NUL byte");
        }
        Effects.send(view.getBytes(UTF_8), java.util.Objects.requireNonNull(bytes));
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
        send("table", config.getBytes(UTF_8));
    }

    public static void send(Image value) {
        String config = "{\"alt\":" + quote(value.alt())
                + ",\"media_type\":" + quote(value.mediaType()) + "}";
        var frame = frame(config, value.bytes().length);
        send("image", frame.put(value.bytes()).array());
    }

    public static void send(Points value) {
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
        send("points", frame.array());
    }

    public static void send(LineChart value) {
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
        send("line-chart", frame.array());
    }

    private static void putFinite(ByteBuffer frame, double value) {
        if (!Double.isFinite(value)) {
            throw new IllegalArgumentException("Line chart axes require finite values");
        }
        frame.putDouble(value);
    }

    private static void sendText(String view, String value) {
        send(view, ("{\"text\":" + quote(value) + "}").getBytes(UTF_8));
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
