def main(input)
  file = 'note.txt'
  text = input.fetch(:text).fetch('value')
  File.write(file, text + "\n")
  {file: file}
end
