using System.IO;
using Tsugumi;

public static class Card
{
    public static Output Run(Input input)
    {
        const string path = "note.txt";
        var (artifact, directory) = tsugumi.ArtifactT.Dir(keep: Params.Current.Keep_output);
        File.WriteAllText(Path.Combine(directory, path), Params.Current.Text);
        return new Output { File = artifact };
    }
}
