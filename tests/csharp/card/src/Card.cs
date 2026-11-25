using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using Google.FlatBuffers;
using Newtonsoft.Json;
using Tsugumi;
using Artifact = tsugumi.ArtifactT;
using outputs;

public static class Card
{
    private static byte[] Id(byte prefix)
    {
        var bytes = new byte[16];
        bytes[0] = prefix;
        return bytes;
    }

    public static Output Run(Input input)
    {
        var (number, forwarded) = input.Arrival switch
        {
            Input_arrival.Number member => (member.Value.Value, member.Value.File),
            Input_arrival.Text member => (20L, member.Value.File),
            _ => throw new InvalidOperationException("Unknown candidate"),
        };
        var opened = forwarded.Open();
        var items = input.Batch.Items;
        var parameters = Params.Current;
        if (input.Direct.Open() != opened || !Directory.Exists(opened) ||
            items.Count != 1 || items[0].Schema != new string('4', 64) ||
            inputs.batch.Element.GetRootAsElement(new ByteBuffer(items[0].Bytes.ToArray())).Value != 4 ||
            !parameters.Flag || parameters.Ratio != 1.0 || parameters.Labels[0] != "日本語" ||
            Effects.Cancelled() || Effects.RemainingMilliseconds() != 500)
            throw new InvalidOperationException("Unexpected contract fixture values");

        if (number < 0) return new Output();

        var (artifact, directory) = Artifact.Dir();
        var file = Path.Combine(directory, "value.txt");
        File.WriteAllText(file, JsonConvert.DeserializeObject<string>("\"test\"")!);
        var uploaded = Artifact.Upload(file);

        var view = Effects.ViewOpen("listing", ViewMode.Window, ViewFlags.HideCursor | ViewFlags.LockPointer, 640, 480);
        var registered = Effects.RouteRegister(view.Session);
        if (!view.Session.AsSpan().SequenceEqual(Id(0x11)) || !view.Route.AsSpan().SequenceEqual(Id(0x22)) ||
            !registered.AsSpan().SequenceEqual(Id(0x33)))
            throw new InvalidOperationException("Unexpected view IDs");
        var message = Effects.Recv();
        if (message.Status != ReceiveStatus.Message || !message.Bytes.AsSpan().SequenceEqual(new byte[]{42}) ||
            !message.Route.AsSpan().SequenceEqual(registered) || !message.Session.AsSpan().SequenceEqual(view.Session) ||
            Effects.Recv(0).Status != ReceiveStatus.Timeout ||
            Effects.Recv(1).Status != ReceiveStatus.Cancelled)
            throw new InvalidOperationException("Unexpected receive status or address");
        Effects.RouteUnregister(registered);

        var bytes = new byte[1 << 24];
        Array.Fill(bytes, (byte)42);
        if (Effects.Send(view.Session, new byte[]{42}) != 0 || Effects.Send(view.Session, bytes) != 0 ||
            Effects.Send(view.Session, new byte[(1 << 24) + 5]) >= 0)
            throw new InvalidOperationException("Unexpected send status");
        Effects.ViewClose(view.Session);
        try
        {
            Effects.RouteRegister(view.Session);
            throw new InvalidOperationException("Registered a route on a closed window");
        }
        catch (Effects.WindowClosedException) {}
        var close = Effects.Recv();
        if (close.Status != ReceiveStatus.Close || close.Bytes.Length != 0 ||
            !close.Route.AsSpan().SequenceEqual(view.Route) || !close.Session.AsSpan().SequenceEqual(view.Session))
            throw new InvalidOperationException("Unexpected close event");
        new Thread(() =>
        {
            var reply = Effects.Recv();
            if (reply.Status == ReceiveStatus.Message)
            {
                var background = Effects.ViewOpen("background");
                if (!background.Route.AsSpan().SequenceEqual(Id(0x55)) ||
                    !background.Session.AsSpan().SequenceEqual(Id(0x44)) ||
                    Effects.Send(background.Session, reply.Bytes) != 0)
                    throw new InvalidOperationException("Unexpected background view");
            }
        }) { IsBackground = true }.Start();

        return new Output
        {
            Listing = new ListingT
            {
                Value = parameters.Offset + number,
                Nested = new ChildT { File = artifact },
                Files = new List<Artifact> { uploaded, forwarded },
                Choice = new ChoiceUnion { Type = Choice.File, Value = Artifact.Dir(keep: false).Artifact },
            },
        };
    }
}
