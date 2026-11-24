def main(input)
  text = Tsugumi.params.fetch('value')
  file, directory = Tsugumi::Artifact.dir
  File.write(File.join(directory, 'note.txt'), text + "\n")
  {file: file}
end
