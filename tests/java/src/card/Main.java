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
        switch (input.params().getMode()) {
            case 1 -> {
                // Two successful calls and five failures preceded this call.
                check(calls == 8);
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
                int fd = input.params().getFd();
                background = new Thread(() -> displayLater(fd));
                background.start();
            }
            case 2 -> {
                background.join();
                check(calls == 9);
            }
            case 3 -> throw new Failure(FailureKind.CANCELLED, "explicit failure", "fit");
            case 4 -> throw new IllegalStateException("standard exception");
            case 5 -> throw new AssertionError("test error");
            case 6 -> { return new Card.Output(null, null, null, null, null); }
            default -> {}
        }

        var text = input.data().getChoice().asText();
        check(text.getText().equals("value"));
        long value = input.data().getRows()[0].getValue();
        var position = new PositionT();
        position.setX(Offset.add(input.number().getPosition().getX(), value));
        position.setY(input.number().getPosition().getY());
        var number = new NumberT();
        number.setPosition(position);

        var row = new RowT();
        row.setValue(value);
        var count = new CountT();
        count.setValue(5);
        var choice = new ChoiceUnion();
        choice.setType(input.params().getMode() == 7 ? Choice.NONE : Choice.Count);
        choice.setValue(count);
        var data = new DataT();
        data.setRows(new RowT[]{row});
        data.setChoice(choice);

        Path result = null;
        if (input.file() != null) {
            result = Path.of("result.txt");
            Files.writeString(result, Files.readString(input.file()));
        }
        return new Card.Output(number, data, result, input.file(), input.directory());
    }

    private static void checkTools() throws IOException, InterruptedException {
        Path sort = Path.of(System.getenv("PATH"), "sort");
        Files.writeString(Path.of("sort.txt"), "b\na\n");
        var process = new ProcessBuilder(sort.toString(), "sort.txt").start();
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
