package card;

import fixture.Offset;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.util.Arrays;
import inputs.batch.Element;
import outputs.ChildT;
import outputs.Choice;
import outputs.ChoiceUnion;
import outputs.ListingT;
import tsugumi.ArtifactT;
import tsugumi.Artifacts;
import tsugumi.Card;
import tsugumi.Display;
import tsugumi.Effects;

public final class Main {
    private Main() {}

    private static ByteBuffer memberBytes(int[] unsignedBytes) {
        var bytes = new byte[unsignedBytes.length];
        for (int index = 0; index < bytes.length; index++) bytes[index] = (byte) unsignedBytes[index];
        return ByteBuffer.wrap(bytes);
    }

    public static Card.Output run(Card.Input input) throws Exception {
        long number;
        ArtifactT granted;
        switch (input.arrival()) {
            case Card.Input_arrival.Number member -> {
                number = member.value().getValue();
                granted = member.value().getFile();
            }
            case Card.Input_arrival.Text member -> {
                number = 20;
                granted = member.value().getFile();
            }
        }
        var opened = granted.open();
        var items = input.batch().getItems();
        var params = Card.params();
        if (!opened.equals(input.direct().open()) || !Files.isDirectory(opened)
                || items.length != 1 || !items[0].getSchema().equals("4".repeat(64))
                || Element.getRootAsElement(memberBytes(items[0].getBytes())).value() != 4
                || !params.flag() || params.ratio() != 1.0 || !params.labels().getFirst().equals("日本語")
                || Effects.cancelled() || Effects.remainingMs() != 500) {
            throw new IllegalStateException("Unexpected contract fixture values");
        }
        if (number < 0) return new Card.Output(null, null);

        var created = Artifacts.dir();
        var file = created.path().resolve("value.txt");
        Files.writeString(file, "test");
        var uploaded = Artifacts.upload(file);
        var sort = new ProcessBuilder("tsugumi-test-sort", file.toString()).start();
        try (var output = sort.inputReader()) {
            if (!"test".equals(output.readLine())) throw new IllegalStateException("Unexpected tool output");
        }
        if (sort.waitFor() != 0) throw new IllegalStateException("Sort tool failed");

        var message = Display.recv("request", -1);
        if (message.status() != Display.ReceiveStatus.MESSAGE || !Arrays.equals(message.bytes(), new byte[]{42})
                || Display.recv("request", 0).status() != Display.ReceiveStatus.TIMEOUT
                || Display.recv("request", 1).status() != Display.ReceiveStatus.CANCELLED) {
            throw new IllegalStateException("Unexpected receive status");
        }

        Display.send("listing", new byte[]{42});
        var data = new byte[(1 << 24) + 5];
        Arrays.fill(data, (byte) 42);
        Display.send("listing", data);
        Thread.ofPlatform().daemon().start(() -> {
            var reply = Display.recv("background", -1);
            if (reply.status() == Display.ReceiveStatus.MESSAGE) Display.send("background", reply.bytes());
        });

        var child = new ChildT();
        child.setFile(created.artifact());
        var choice = new ChoiceUnion();
        choice.setType(Choice.File);
        choice.setValue(Artifacts.dir().artifact());
        var listing = new ListingT();
        listing.setValue(Offset.add(params.offset(), number));
        listing.setNested(child);
        listing.setFiles(new ArtifactT[]{uploaded, granted});
        listing.setChoice(choice);
        return new Card.Output(listing, null);
    }
}
