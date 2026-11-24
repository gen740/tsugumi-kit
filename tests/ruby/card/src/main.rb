require 'rake'

def main(input)
  raise "locked gem was not activated" unless Rake::VERSION == "13.2.1"
  item = input.fetch(:arrival).fetch("value")
  known = item.fetch("file")
  unless File.directory?(known.open) && input.fetch(:direct).open == known.open
    raise "unexpected Artifact input"
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
  choice, = Tsugumi::Artifact.dir(keep: false)
  opener, session = Tsugumi.view_open("listing", Tsugumi::WINDOW, Tsugumi::HIDE_CURSOR | Tsugumi::LOCK_POINTER, 640, 480)
  raise "wrong opened view" unless opener == "\x22".b.ljust(16, "\0") && session == "\x11".b.ljust(16, "\0")
  route = Tsugumi.route_register(session)
  raise "wrong route" unless route == "\x33".b.ljust(16, "\0")
  raise "wrong message" unless Tsugumi.recv == [:message, route, session, "*"]
  raise "wrong timeout" unless Tsugumi.recv(0) == [:timeout, nil, nil, nil]
  raise "wrong cancellation" unless Tsugumi.recv(1) == [:cancelled, nil, nil, nil]
  Tsugumi.route_unregister(route)
  Tsugumi.send(session, "*")
  Tsugumi.send(session, "*" * (1 << 24))
  begin
    Tsugumi.send(session, "*" * ((1 << 24) + 5))
  rescue RuntimeError => error
    raise unless error.message == "view message was rejected"
  else
    raise "oversize send succeeded"
  end
  raise "wrong close" unless Tsugumi.recv == [:close, opener, session, "".b]
  Thread.new do
    kind, _, _, bytes = Tsugumi.recv
    if kind == :message
      route, session = Tsugumi.view_open("background")
      raise "wrong background view" unless route == "\x55".b.ljust(16, "\0") && session == "\x44".b.ljust(16, "\0")
      Tsugumi.send(session, bytes)
    end
  end

  {listing: {nested: {file: artifact}, files: [upload, known],
             choice: {type: "File", value: choice}, value: params.fetch("offset") + number}}
end
