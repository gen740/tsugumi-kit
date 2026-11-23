package card;

import java.io.IOException;
import java.nio.file.Files;
import tsugumi.Artifacts;
import tsugumi.Card;
import tsugumi.Display;

public final class Main {
    public static Card.Output run(Card.Input input) throws IOException {
        String text = input.text().getValue();
        var created = Artifacts.dir();
        Files.writeString(created.path().resolve("message.txt"), text);
        Display.send(new Display.Text(text));
        return new Card.Output(created.artifact());
    }
}
