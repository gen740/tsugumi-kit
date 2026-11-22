using System.IO;
using Tsugumi;

public static class Card
{
    public static Output Run(Input input)
    {
        const string path = "note.txt";
        File.WriteAllText(path, input.Params.Text);
        return new Output { File = path };
    }
}
