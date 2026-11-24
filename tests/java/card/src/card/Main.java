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

    private static byte[] id(int prefix) {
        var bytes = new byte[16];
        bytes[0] = (byte)prefix;
        return bytes;
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

        var view = Display.viewOpen("listing", Display.ViewMode.WINDOW, 640, 480);
        var registered = Display.routeRegister(view.session());
        if (!Arrays.equals(view.session(), id(0x11)) || !Arrays.equals(view.route(), id(0x22))
                || !Arrays.equals(registered, id(0x33))) {
            throw new IllegalStateException("Unexpected view IDs");
        }
        var message = Display.recv(-1);
        if (message.status() != Display.ReceiveStatus.MESSAGE || !Arrays.equals(message.bytes(), new byte[]{42})
                || !Arrays.equals(message.route(), registered) || !Arrays.equals(message.session(), view.session())
                || Display.recv(0).status() != Display.ReceiveStatus.TIMEOUT
                || Display.recv(1).status() != Display.ReceiveStatus.CANCELLED) {
            throw new IllegalStateException("Unexpected receive status or address");
        }
        Display.routeUnregister(registered);

        Display.send(view.session(), new byte[]{42});
        var data = new byte[1 << 24];
        Arrays.fill(data, (byte)42);
        Display.send(view.session(), data);
        try {
            Display.send(view.session(), new byte[(1 << 24) + 5]);
            throw new AssertionError("Oversized send succeeded");
        } catch (IllegalStateException expected) {
            // The ABI rejects an oversized raw message.
        }
        var close = Display.recv(-1);
        if (close.status() != Display.ReceiveStatus.CLOSE || close.bytes().length != 0
                || !Arrays.equals(close.route(), view.route()) || !Arrays.equals(close.session(), view.session())) {
            throw new IllegalStateException("Unexpected close event");
        }
        Thread.ofPlatform().daemon().start(() -> {
            var reply = Display.recv(-1);
            if (reply.status() == Display.ReceiveStatus.MESSAGE) {
                var background = Display.viewOpen("background");
                if (!Arrays.equals(background.route(), id(0x55)) || !Arrays.equals(background.session(), id(0x44))) {
                    throw new IllegalStateException("Unexpected background view IDs");
                }
                Display.send(background.session(), reply.bytes());
            }
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
