package tsugumi;

import com.google.flatbuffers.FlatBufferBuilder;
import java.nio.ByteBuffer;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.function.ToIntFunction;
import parameters.Params;
import parameters.ParamsT;
import tsugumi.wire.Tsugumi.Card.CardFailure;
import tsugumi.wire.Tsugumi.Card.CardInput;
import tsugumi.wire.Tsugumi.Card.CardPort;
import tsugumi.wire.Tsugumi.Card.CardResult;
import tsugumi.wire.Tsugumi.Data.PortKind;

import static java.nio.charset.StandardCharsets.UTF_8;

final class Wire {
    private Wire() {}

    // The native adapter has verified the envelope, port kinds and payloads.
    static final class Inputs {
        private final CardInput input;
        private final Map<String, CardPort> ports = new HashMap<>();

        Inputs(byte[] bytes) {
            input = CardInput.getRootAsCardInput(ByteBuffer.wrap(bytes));
            for (int index = 0; index < input.inputsLength(); index++) {
                var port = input.inputs(index);
                ports.put(port.name(), port);
            }
        }

        ParamsT params() {
            return Params.getRootAsParams(input.paramsAsByteBuffer().slice()).unpack();
        }

        // An input that did not arrive stays absent from the values.
        void read(String name, Function<ByteBuffer, Object> decode, Map<String, Object> values) {
            CardPort port = ports.get(name);
            if (port != null) values.put(name, decode.apply(port.bytesAsByteBuffer().slice()));
        }

        void path(String name, Map<String, Object> values) {
            CardPort port = ports.get(name);
            if (port == null) return;
            try {
                values.put(name, Path.of(port.path()));
            } catch (InvalidPathException error) {
                throw new Failure(FailureKind.INVALID_REQUEST, "Invalid Path input " + name);
            }
        }
    }

    static byte[] pack(String name, ToIntFunction<FlatBufferBuilder> pack) {
        try {
            var payload = new FlatBufferBuilder();
            payload.finish(pack.applyAsInt(payload));
            return payload.sizedByteArray();
        } catch (RuntimeException | AssertionError | StackOverflowError error) {
            throw new Failure(FailureKind.OUTPUT_INVALID,
                    "Invalid output " + name + ": " + error);
        }
    }

    static final class Outputs {
        private final FlatBufferBuilder result = new FlatBufferBuilder();
        private final ArrayList<Integer> offsets = new ArrayList<>();

        void add(String name, byte[] bytes) {
            offsets.add(CardPort.createCardPort(result, result.createString(name),
                    PortKind.FlatBuffer, CardPort.createBytesVector(result, bytes), 0));
        }

        void path(String name, Path path) {
            if (path == null) return;
            String value = path.toString();
            if (value.isEmpty()) {
                throw new Failure(FailureKind.OUTPUT_INVALID, "Invalid Path output " + name);
            }
            offsets.add(CardPort.createCardPort(result, result.createString(name),
                    PortKind.Path, 0, result.createString(value)));
        }

        byte[] finish() {
            int[] values = offsets.stream().mapToInt(Integer::intValue).toArray();
            int ports = CardResult.createOutputsVector(result, values);
            result.finish(CardResult.createCardResult(result, ports, 0));
            return result.sizedByteArray();
        }
    }

    static byte[] failure(Throwable error, Set<String> sources) {
        var builder = new FlatBufferBuilder();
        FailureKind kind = FailureKind.WORKER_FAILED;
        String cell = null;
        String message;
        if (error instanceof Failure failure) {
            kind = failure.kind();
            message = failure.getMessage();
            cell = failure.cell();
        } else {
            message = describe(error);
        }

        int source = 0;
        int line = 0;
        for (StackTraceElement frame : error.getStackTrace()) {
            String file = frame.getFileName();
            if (file == null || frame.getLineNumber() <= 0) continue;
            String cls = frame.getClassName();
            int boundary = cls.lastIndexOf('.') + 1;
            String path = cls.substring(0, boundary).replace('.', '/') + file;
            if (sources.contains(path)) {
                source = builder.createString("src/" + path);
                line = frame.getLineNumber();
                break;
            }
        }
        int failure = CardFailure.createCardFailure(builder, kind.value,
                builder.createString(ByteBuffer.wrap(message.getBytes(UTF_8))), source, line,
                cell == null ? 0 : builder.createString(ByteBuffer.wrap(cell.getBytes(UTF_8))));
        builder.finish(CardResult.createCardResult(builder, 0, failure));
        return builder.sizedByteArray();
    }

    private static String describe(Throwable error) {
        String name = error.getClass().getName();
        try {
            String message = error.getMessage();
            return message == null ? name : name + ": " + message;
        } catch (Throwable ignored) {
            return name;
        }
    }
}
