require 'json'

module Tsugumi
  class Failure < StandardError
    attr_reader :kind, :cell

    def initialize(kind, message, cell: nil)
      super(message)
      @kind = kind
      @cell = cell
    end
  end

  # Every input may be absent: an unconnected input never arrives.
  class Inputs
    def initialize(ports)
      @ports = ports
    end

    # An optional input, or nil when it is absent.
    def [](name) = @ports[name.to_s]

    # A needed input. Without a fallback, an absent input fails the call with
    # input_missing naming the port.
    def fetch(name, *fallback, &block)
      name = name.to_s
      return @ports.fetch(name, *fallback, &block) if @ports.key?(name) || !fallback.empty? || block
      raise Failure.new(:input_missing, "Missing input #{name}")
    end
  end

  class Artifact
    def initialize(id)
      @id = id.freeze
      freeze
    end

    # keep: false makes an Artifact that exists only while its run runs.
    def self.dir(keep: true)
      id, path = TsugumiHost.artifact_dir(keep)
      [new(id), path]
    end

    def self.upload(path, keep: true) = new(TsugumiHost.artifact_upload(File.path(path), keep))
    def open = TsugumiHost.artifact_open(@id)

    def to_h = {"$artifact" => @id.bytes}
  end

  def self.params = PARAMS
  private_constant :PARAMS

  PANEL = 0
  WINDOW = 1
  FULLSCREEN = 2

  # Return the opener's route and the new session as 16-byte Strings.
  HIDE_CURSOR = 1
  LOCK_POINTER = 2

  def self.view_open(view, mode = PANEL, flags = 0, width = 0, height = 0)
    TsugumiHost.view_open(view, mode, flags, width, height)
  end

  WindowClosed = TsugumiHost::WindowClosed

  def self.view_close(session) = TsugumiHost.view_close(session)
  def self.route_register(session) = TsugumiHost.route_register(session)
  def self.route_unregister(route) = TsugumiHost.route_unregister(route)
  def self.recv(timeout_ms = -1) = TsugumiHost.recv(timeout_ms)

  Markdown = Struct.new(:text)
  Scalar = Struct.new(:value, :label)
  Table = Struct.new(:columns, :rows)
  Image = Struct.new(:bytes, :media_type, :alt)
  Points = Struct.new(:x, :y, :z, :colors)
  LineChart = Struct.new(:series)
  Plotly = Struct.new(:config)
  VegaLite = Struct.new(:config)

  def self.cancelled = TsugumiHost.cancelled
  def self.remaining_ms = TsugumiHost.remaining_ms
  def self.bytes(value) = [value].pack('m0')
  def self.decode_bytes(value) = value.unpack1('m0')

  # Send opaque bytes to a session, or open the bundled view for a typed value.
  def self.send(value, *bytes)
    return TsugumiHost.send_bytes(value, *bytes, false) unless bytes.empty?

    view, payload = display_value(value)
    _, session = view_open(view)
    TsugumiHost.send_bytes(session, payload, true)
  end

  # Send a typed value to an already open bundled HTML view.
  def self.send_value(session, value)
    _, payload = display_value(value)
    TsugumiHost.send_bytes(session, payload, true)
  end

  def self.display_value(value)
    case value
    when String then configured('text', {text: value})
    when Markdown then configured('markdown', {text: value.text})
    when Numeric then scalar_view(value)
    when Scalar then scalar_view(value.value, value.label)
    when Table then table_view(value)
    when Image
      configured('image', {media_type: value.media_type || 'image/png', alt: value.alt || ''}, value.bytes)
    when Points then points_view(value)
    when LineChart then chart_view(value.series)
    when Plotly then configured('plotly', value.config)
    when VegaLite then configured('vega-lite', value.config)
    else raise ArgumentError, "cannot display #{value.class}"
    end
  end

  def self.configured(name, config, *parts)
    data = JSON.generate(config).b
    unless parts.empty?
      raise ArgumentError, 'display metadata is too large' if data.bytesize > 0xffffffff
      data = [data.bytesize].pack('L<') + data + parts.join.b
    end
    [name, data]
  end

  def self.scalar_view(value, label = nil)
    number = Float(value)
    text = number.finite? ? format('%.6g', number) : "\u2014"
    configured('markdown', {text: label.nil? || label.empty? ? text : "**#{label}**: #{text}"})
  end

  def self.table_view(table)
    columns = table.columns.map(&:to_s)
    rows = table.rows.map do |row|
      raise ArgumentError, 'table row width differs from columns' unless row.length == columns.length
      row.map { |cell| cell.is_a?(String) ? cell : JSON.generate(json_value(cell)) }
    end
    configured('table', {columns: columns, rows: rows})
  end

  def self.points_view(points)
    count = points.x.length
    colors = points.colors || []
    unless points.y.length == count && points.z.length == count &&
           (colors.empty? || colors.length == count) &&
           colors.all? { |color| color.is_a?(Integer) && color.between?(0, 0xffffff) }
      raise ArgumentError, 'points require matching x, y, z and optional RGB colors'
    end
    configured('points', {count: count},
               points.x.pack('e*'), points.y.pack('e*'), points.z.pack('e*'), colors.pack('L<*'))
  end

  def self.chart_view(series)
    x = []
    y = []
    rows = series.map do |one|
      row = one.to_h.transform_keys(&:to_s)
      id = row.fetch('id')
      xs = row.fetch('x').map { |v| Float(v) }
      ys = row.fetch('y').map { |v| Float(v) }
      unless xs.length == ys.length && xs.all?(&:finite?) && ys.all?(&:finite?)
        raise ArgumentError, 'line chart axes require matching finite values'
      end
      config = {id: id, label: row.fetch('label', id), offset: x.length, count: xs.length}
      x.concat(xs)
      y.concat(ys)
      config
    end
    configured('line-chart', {series: rows}, x.pack('E*'), y.pack('E*'))
  end

  def self.json_value(value, remaining_depth = 100)
    raise ArgumentError, 'value nesting is too deep' if remaining_depth&.negative?
    remaining_depth -= 1 if remaining_depth
    case value
    when Hash
      value.each_with_object({}) do |(key, item), out|
        key = key.to_s if key.is_a?(Symbol)
        raise ArgumentError, 'expected a string field name' unless key.is_a?(String)
        raise ArgumentError, "duplicate field #{key}" if out.key?(key)
        out[key] = json_value(item, remaining_depth)
      end
    when Array then value.map { |item| json_value(item, remaining_depth) }
    when String, Integer, TrueClass, FalseClass, NilClass then value
    when Float
      return 'NaN' if value.nan?
      return value.positive? ? 'Infinity' : '-Infinity' if value.infinite?
      value
    else
      object = value.to_h if value.respond_to?(:to_h)
      raise ArgumentError, "expected a value or #to_h for #{value.class}" unless object.is_a?(Hash)
      json_value(object, remaining_depth)
    end
  end

  def self.load_card(directory, prefix)
    $stdout.sync = $stderr.sync = true
    file = File.join(directory, 'main.rb')
    klass = Class.new
    klass.const_set(:Tsugumi, self)
    klass.class_eval(File.read(file, encoding: Encoding::UTF_8), file, 1)
    @card = klass.new
    raise 'define main(input) in main.rb' unless @card.respond_to?(:main) && @card.method(:main).arity == 1
    nil
  rescue Exception => error # Card load errors, including SyntaxError and SystemExit.
    failure(error, directory, prefix)
  end

  def self.failure(error, directory, prefix, kind = 'worker_failed')
    root = directory + '/'
    location = error.backtrace_locations&.find { |frame| (frame.absolute_path || frame.path).start_with?(root) }
    source = location && (location.absolute_path || location.path)
    line = location&.lineno
    if !source && error.is_a?(SyntaxError) && (match = error.message.match(/\A(.+?):(\d+):/))
      source, line = match[1], match[2].to_i if match[1].start_with?(root)
    end
    result = {
      kind: error.is_a?(Failure) ? error.kind.to_s : kind,
      message: (error.is_a?(Failure) ? error.message : "#{error.class}: #{error.message}")
        .encode(Encoding::UTF_8, invalid: :replace, undef: :replace)
    }
    result.merge!(source: prefix + source.delete_prefix(root), line: line) if source
    result[:cell] = error.cell.to_s if error.is_a?(Failure) && error.cell
    result
  end

  def self.decode_input(value)
    case value
    when Hash
      return Artifact.new(value.fetch('$artifact').pack('C*')) if value.key?('$artifact')

      value.transform_values { |item| decode_input(item) }
    when Array then value.map { |item| decode_input(item) }
    else value
    end
  end

  def self._dispatch(input_json, directory, prefix)
    @load_failure = load_card(directory, prefix) unless defined?(@load_failure)
    return JSON.generate({failure: @load_failure}) if @load_failure

    begin
      input = JSON.parse(input_json, max_nesting: false)
    rescue JSON::ParserError => error
      return JSON.generate({failure: failure(error, directory, prefix, 'invalid_request')})
    end
    begin
      output = @card.main(Inputs.new(decode_input(input.fetch('ports'))))
    rescue Exception => error # Includes SystemExit raised by Card code.
      return JSON.generate({failure: failure(error, directory, prefix)})
    end
    # JSON nesting counts arrays and union wrappers as well as FlatBuffers tables.
    output = json_value(output, nil)
    raise ArgumentError, 'main must return a Hash of output ports' unless output.is_a?(Hash)
    JSON.generate({outputs: output}, max_nesting: false)
  rescue StandardError, SystemStackError => error
    JSON.generate({failure: failure(error, directory, prefix, 'output_invalid')})
  end

  private_class_method :display_value, :scalar_view, :table_view, :points_view, :chart_view,
                       :load_card, :failure, :json_value
end
