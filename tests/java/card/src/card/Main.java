package card;

import fixture.Offset;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import outputs.data.Choice;
import outputs.data.ChoiceUnion;
import outputs.data.CountT;
import outputs.data.DataT;
import outputs.data.RowT;
import outputs.number.NumberT;
import outputs.number.PositionT;
import inputs.arrival.tsugumi.merge.t0.TemperatureT;
import inputs.arrival.tsugumi.merge.t1.ReadingT;
import tsugumi.Card;
import tsugumi.Display;
import tsugumi.Effects;
import tsugumi.Failure;
import tsugumi.FailureKind;

public final class Main {
    private static Thread background;
    private static int calls;

    public static Card.Output run(Card.Input input) throws Exception {
        ++calls;
        switch (Math.toIntExact(input.params().getMode())) {
            case 1 -> {
                // Three scenario calls and five failures preceded this call.
                check(calls == 9);
                check(System.getProperty("user.home").equals(System.getenv("HOME")));
                check(System.getProperty("java.io.tmpdir").equals(System.getenv("TMPDIR")));
                checkTools();
                Thread queries = new Thread(() -> check(!Effects.cancelled() && Effects.remainingMs() == 500));
                var errors = new AtomicReference<Throwable>();
                queries.setUncaughtExceptionHandler((thread, error) -> errors.set(error));
                queries.start();
                queries.join();
                if (errors.get() != null) throw new AssertionError(errors.get());
                display();
                int fd = Math.toIntExact(input.params().getFd());
                background = new Thread(() -> displayLater(fd));
                background.start();
            }
            case 2 -> {
                background.join();
                check(calls == 10);
            }
            case 3 -> throw new Failure(FailureKind.CANCELLED, "explicit failure", "fit");
            case 4 -> throw new IllegalStateException("standard exception");
            case 5 -> throw new AssertionError("test error");
            case 6 -> { return new Card.Output(null, null, null, null, null, null); }
            default -> {}
        }

        var data = input.get("data", inputs.data.DataT.class);
        // A needed input: absent produces INPUT_MISSING naming the port.
        var number = input.get("number", inputs.number.NumberT.class);
        check(input.number().orElseThrow() == number);
        check(input.find("unwired", Path.class).isEmpty());
        if (input.params().getMode() == 20) input.get("unwired", Path.class);
        if (input.params().getMode() == 22) input.get("number", Path.class);

        var text = data.getChoice().asText();
        check(text.getText().equals("value"));
        long value = data.getRows()[0].getValue();
        var position = new PositionT();
        position.setX(Offset.add(number.getPosition().getX(), value));
        position.setY(number.getPosition().getY());
        var outputNumber = new NumberT();
        outputNumber.setPosition(position);

        var row = new RowT();
        row.setValue(value);
        var count = new CountT();
        count.setValue(5);
        var choice = new ChoiceUnion();
        choice.setType(input.params().getMode() == 7 ? Choice.NONE : Choice.Count);
        choice.setValue(count);
        var outputData = new DataT();
        outputData.setRows(new RowT[]{row});
        outputData.setChoice(choice);

        Path file = input.file().orElse(null);
        Path result = null;
        if (file != null) {
            result = Path.of("result.txt");
            Files.writeString(result, Files.readString(file));
        }
        Object arrival = switch (input.arrival().orElse(null)) {
            case TemperatureT temperature -> {
                var next = new outputs.arrival.tsugumi.merge.t0.TemperatureT();
                next.setValue(temperature.getValue() + 1);
                yield next;
            }
            case ReadingT reading -> {
                var next = new outputs.arrival.tsugumi.merge.t1.ReadingT();
                next.setName(reading.getName());
                yield next;
            }
            case null -> null;
            default -> throw new AssertionError("Unexpected Merge member");
        };
        if (input.params().getMode() == 21) arrival = "not a Merge member";
        // Return input paths unchanged to forward them.
        return new Card.Output(outputNumber, outputData, result, file,
                input.find("directory", Path.class).orElse(null), arrival);
    }

    private static void checkTools() throws IOException, InterruptedException {
        String sort = "tsugumi-test-sort";
        check(Files.isSymbolicLink(Path.of(System.getenv("PATH"), sort)));
        Files.writeString(Path.of("sort.txt"), "b\na\n");
        var process = new ProcessBuilder(sort, "sort.txt").start();
        try (var output = process.inputReader()) {
            check("a".equals(output.readLine()) && "b".equals(output.readLine()));
        }
        check(process.waitFor() == 0);
    }

    private static void displayLater(int fd) {
        try (var start = Files.newInputStream(Path.of("/dev/fd", Integer.toString(fd)))) {
            check(start.read() == '!');
            check(Effects.cancelled() && Effects.remainingMs() == 0);
            Display.send("outside");
        } catch (IOException error) {
            throw new AssertionError(error);
        }
    }

    private static void display() {
        Display.send(new Display.Text("résumé\n\""));
        Display.send(new Display.Markdown("**text**"));
        Display.send(new Display.Table(List.of("n"), List.of(List.of(1))));
        byte[] raw = {0, 13, (byte) 255};
        Display.send(new Display.Image(raw, "image/png", "sample"));
        Display.send(new Display.Points(new float[]{1, -2}, new float[]{3, 4},
                new float[]{5, 6}, new int[]{0x123456, 0xaabbccdd}));
        Display.send(new Display.LineChart(List.of(
                new Display.LineSeries("a", "first", new double[]{2, -4}, new double[]{3, 5}),
                new Display.LineSeries("b", "second", new double[]{6}, new double[]{-7}))));
        Display.send("custom-🎴", raw);
    }

    private static void check(boolean passed) {
        if (!passed) throw new AssertionError("Unexpected Java Card state");
    }
}
