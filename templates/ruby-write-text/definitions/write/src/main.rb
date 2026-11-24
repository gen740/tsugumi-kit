def main(input)
  text = Tsugumi.params.fetch('value')
  file, directory = Tsugumi::Artifact.dir(keep: Tsugumi.params.fetch('keep_output'))
  File.write(File.join(directory, 'note.txt'), text + "\n")
  {file: file}
end
