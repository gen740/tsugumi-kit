require 'rake'

def main(input)
  raise "locked gem was not activated" unless Rake::VERSION == "13.2.1"
  item = input.fetch(:arrival).fetch("value")
  granted = item.fetch("file")
  unless File.directory?(granted.open) && input.fetch(:direct).open == granted.open
    raise "unexpected Artifact grant"
  end
  number = item.fetch("value")
  number = 20 unless number.is_a?(Integer)
  batch = input.fetch(:batch).fetch("items")
  envelope = batch.fetch(0)
  data = Tsugumi.decode_bytes(envelope.fetch("bytes"))
  table = data.unpack1("l<")
  vtable = table - data.unpack1("l<", offset: table)
  offset = data.unpack1("S<", offset: vtable + 4)
  params = Tsugumi.params
  raise "mutable params" unless params.frozen? && params.fetch("labels").frozen?
  unless batch.size == 1 && envelope.fetch("schema") == "4" * 64 &&
         data.unpack1("l<", offset: table + offset) == 4 &&
         params.fetch("flag") && params.fetch("ratio").eql?(1.0) &&
         params.fetch("labels").first == "日本語" &&
         !Tsugumi.cancelled && Tsugumi.remaining_ms == 500
    raise "unexpected contract fixture values"
  end
  return {} if number < 0

  artifact, directory = Tsugumi::Artifact.dir
  file = File.join(directory, "value.txt")
  File.write(file, "test")
  upload = Tsugumi::Artifact.upload(file)
  choice, = Tsugumi::Artifact.dir
  raise "wrong message" unless Tsugumi.recv("request") == [:message, "*"]
  raise "wrong timeout" unless Tsugumi.recv("request", 0) == [:timeout, nil]
  raise "wrong cancellation" unless Tsugumi.recv("request", 1) == [:cancelled, nil]
  Tsugumi.send("listing", "*")
  Tsugumi.send("listing", "*" * ((1 << 24) + 5))
  Thread.new do
    status, bytes = Tsugumi.recv("background")
    Tsugumi.send("background", bytes) if status == :message
  end

  {listing: {nested: {file: artifact}, files: [upload, granted],
             choice: {type: "File", value: choice}, value: params.fetch("offset") + number}}
end
