def main(input)
  text = input.fetch(:text).fetch('value')
  file, directory = Tsugumi::Artifact.dir
  File.write(File.join(directory, 'note.txt'), text + "\n")
  {file: file}
end
