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
    public static Output Run(Input input)
    {
        var (number, granted) = input.Arrival switch
        {
            Input_arrival.Number member => (member.Value.Value, member.Value.File),
            Input_arrival.Text member => (20L, member.Value.File),
            _ => throw new InvalidOperationException("Unknown candidate"),
        };
        var opened = granted.Open();
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

        var message = Effects.Recv("request", -1);
        if (message.Status != ReceiveStatus.Message || !message.Bytes.AsSpan().SequenceEqual(new byte[]{42}) ||
            Effects.Recv("request", 0).Status != ReceiveStatus.Timeout ||
            Effects.Recv("request", 1).Status != ReceiveStatus.Cancelled)
            throw new InvalidOperationException("Unexpected receive status");

        var bytes = new byte[(1 << 24) + 5];
        Array.Fill(bytes, (byte)42);
        if (Effects.Send("listing", new byte[]{42}) != 0 || Effects.Send("listing", bytes) != 0)
            throw new InvalidOperationException("Unexpected send status");
        new Thread(() =>
        {
            var reply = Effects.Recv("background");
            if (reply.Status == ReceiveStatus.Message) Effects.Send("background", reply.Bytes);
        }) { IsBackground = true }.Start();

        return new Output
        {
            Listing = new ListingT
            {
                Value = parameters.Offset + number,
                Nested = new ChildT { File = artifact },
                Files = new List<Artifact> { uploaded, granted },
                Choice = new ChoiceUnion { Type = Choice.File, Value = Artifact.Dir().Artifact },
            },
        };
    }
}
