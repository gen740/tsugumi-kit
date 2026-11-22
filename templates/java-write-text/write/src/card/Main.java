package card;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import tsugumi.Card;
import tsugumi.Display;

public final class Main {
    public static Card.Output run(Card.Input input) throws IOException {
        String text = input.text().getValue();
        Path file = Files.writeString(Path.of("message.txt"), text);
        Display.send(new Display.Text(text));
        return new Card.Output(file);
    }
}
