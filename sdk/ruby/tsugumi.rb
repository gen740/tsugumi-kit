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

  class Inputs
    attr_reader :params

    def initialize(ports, params)
      @ports = ports
      @params = params
    end

    def [](name) = @ports[name.to_s]
    def fetch(name, ...) = @ports.fetch(name.to_s, ...)
    def path(name) = self[name]&.fetch('value')

    private

    def method_missing(name, *args)
      return self[name] if args.empty? && @ports.key?(name.to_s)
      super
    end

    def respond_to_missing?(name, include_private = false)
      @ports.key?(name.to_s) || super
    end
  end

  Markdown = Struct.new(:text)
  Scalar = Struct.new(:value, :label)
  Table = Struct.new(:columns, :rows)
  Image = Struct.new(:bytes, :media_type, :alt)
  Points = Struct.new(:x, :y, :z, :colors)
  LineChart = Struct.new(:series)
  Plotly = Struct.new(:config)
  VegaLite = Struct.new(:config)
  View = Struct.new(:module_id, :config, :blobs)

  def self.cancelled = TsugumiHost.cancelled
  def self.remaining_ms = TsugumiHost.remaining_ms
  def self.bytes(value) = [value].pack('m0')
  def self.decode_bytes(value) = value.unpack1('m0')

  def self.display(value, record: false)
    view = case value
           when String then View.new('tsugumi:text', {text: value})
           when Markdown then View.new('tsugumi:markdown', {text: value.text})
           when Numeric then scalar_view(value)
           when Scalar then scalar_view(value.value, value.label)
           when Table then table_view(value)
           when Image
             View.new('tsugumi:image', {media_type: value.media_type || 'image/png', alt: value.alt || ''},
                      {'image' => value.bytes})
           when Points then points_view(value)
           when LineChart then chart_view(value.series)
           when Plotly then View.new('tsugumi:plotly', value.config)
           when VegaLite then View.new('tsugumi:vega-lite', value.config)
           when View then value
           else raise ArgumentError, "cannot display #{value.class}"
           end
    TsugumiHost.display(view.module_id, JSON.generate(view.config), (view.blobs || {}).to_a, record)
  end

  def self.scalar_view(value, label = nil)
    number = Float(value)
    View.new('tsugumi:scalar', {value: number.finite? ? number : nil, label: label || ''})
  end

  def self.table_view(table)
    columns = table.columns.map(&:to_s)
    rows = table.rows.map do |row|
      raise ArgumentError, 'table row width differs from columns' unless row.length == columns.length
      row.map { |cell| cell.is_a?(String) ? cell : JSON.generate(json_value(cell)) }
    end
    View.new('tsugumi:table', {columns: columns, rows: rows})
  end

  def self.points_view(points)
    count = points.x.length
    colors = points.colors || []
    unless points.y.length == count && points.z.length == count &&
           (colors.empty? || colors.length == count) &&
           colors.all? { |color| color.is_a?(Integer) && color.between?(0, 0xffffff) }
      raise ArgumentError, 'points require matching x, y, z and optional RGB colors'
    end
    View.new('tsugumi:points', {count: count},
             {'x' => points.x.pack('e*'), 'y' => points.y.pack('e*'),
              'z' => points.z.pack('e*'), 'colors' => colors.pack('L<*')})
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
    View.new('tsugumi:line-chart', {series: rows}, {'x' => x.pack('E*'), 'y' => y.pack('E*')})
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

  def self._dispatch(input_json, directory, prefix)
    @load_failure = load_card(directory, prefix) unless defined?(@load_failure)
    return JSON.generate({failure: @load_failure}) if @load_failure

    begin
      input = JSON.parse(input_json, max_nesting: false)
    rescue JSON::ParserError => error
      return JSON.generate({failure: failure(error, directory, prefix, 'invalid_request')})
    end
    begin
      output = @card.main(Inputs.new(input.fetch('ports'), input.fetch('params')))
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

  private_class_method :scalar_view, :table_view, :points_view, :chart_view,
                       :load_card, :failure, :json_value
end
