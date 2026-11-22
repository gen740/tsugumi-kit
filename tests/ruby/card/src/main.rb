require 'pathname'

def show_values(fd)
  raise 'cancelled during the call' if Thread.new { Tsugumi.cancelled }.value
  raise 'missing deadline' unless Tsugumi.remaining_ms == 500

  Tsugumi.send("résumé\n\"")
  Tsugumi.send(Tsugumi::Markdown.new('**text**'))
  Tsugumi.send(2.5)
  Tsugumi.send(Tsugumi::Table.new(['n'], [[1]]))
  binary = "\x00\x0d\xff".b
  Tsugumi.send(Tsugumi::Image.new(binary, 'image/png', 'sample'))
  Tsugumi.send(Tsugumi::Points.new([1.0], [-2.0], [3.5], [0x123456]))
  Tsugumi.send(Tsugumi::LineChart.new([{id: 'id', x: [2.0], y: [-3.5], label: 'line'}]))
  Tsugumi.send(Tsugumi::Plotly.new({data: []}))
  Tsugumi.send(Tsugumi::VegaLite.new({mark: 'point'}))
  Tsugumi.send('custom', binary)

  Thread.new do
    IO.for_fd(fd, autoclose: false).read(1)
    raise 'expected cancelled and zero time outside the call' unless Tsugumi.cancelled && Tsugumi.remaining_ms.zero?
    Tsugumi.send('outside')
  end
end

def check_child_environment
  keys = ENV.keys.grep(/\A(?:RUBYOPT|RUBYLIB|GEM_HOME|GEM_PATH)\z|\ABUNDL/)
  child = IO.popen([RbConfig.ruby, '--disable=rubyopt', '-e',
                    'ARGV.each { |key| STDOUT.puts ENV.fetch(key) }', *keys], &:read)
  raise 'child has a different environment' unless $?.success? && child.lines(chomp: true) == keys.map { |key| ENV.fetch(key) }

  return unless ENV.fetch('TSUGUMI_TEST_LOCKED') == '1'
  require 'rake'
  spec = Gem.loaded_specs.fetch('rake')
  raise 'locked gem did not load' unless spec.version.to_s == '13.2.1' &&
                                      spec.loaded_from.start_with?(Gem.dir + '/specifications/') &&
                                      Gem.dir.end_with?("/installed/ruby/#{RbConfig::CONFIG.fetch('ruby_version')}")
end

def main(input)
  @calls = (@calls || 0) + 1
  unless input.params.values_at('unread', 'high', 'low') == %w[NaN Infinity -Infinity]
    raise 'expected non-finite parameter defaults'
  end
  case input.params.fetch('mode')
  when 1 then show_values(input.params.fetch('fd'))
  when 2 then raise 'interpreter state was lost' unless @calls > 1
  when 3 then raise Tsugumi::Failure.new(:cancelled, 'explicit failure', cell: 'fit')
  when 4 then raise 'standard exception'
  when 5 then exit 2
  when 6 then return {}
  when 7 then return {number: {value: 2**64}, rows: input[:rows]}
  when 8 then return {number: {value: 10**400}, rows: input[:rows]}
  when 9 then Tsugumi.cancelled
  when 10 then Tsugumi.remaining_ms
  when 11 then check_child_environment
  when 13
    recursive = []
    recursive << recursive
    return {rows: recursive}
  when 14 then input.fetch(:unwired)
  end
  raise 'an unwired input should be absent' unless input[:unwired].nil?

  fallback = Object.new
  if input[:file]
    raise 'fetch used its fallback for a present port' unless input.fetch(:file, fallback) == input[:file]
    raise 'fetch ignored the present port' unless input.fetch('file') { raise 'unexpected fallback' } == input[:file]
  else
    raise 'fetch ignored its fallback value' unless input.fetch(:file, fallback).equal?(fallback)
    raise 'fetch ignored its fallback block' unless input.fetch(:file) { |name| name } == 'file'
    begin
      input.fetch(:file)
      raise 'fetch accepted a missing port without a fallback'
    rescue Tsugumi::Failure => error
      raise 'expected input_missing' unless error.kind == :input_missing && error.message == 'Missing input file'
    end
  end

  number = input.fetch(:number)
  rows = input.fetch(:rows).fetch('rows')
  raise 'expected non-finite tokens' unless rows.first.fetch('readings') == %w[NaN Infinity -Infinity]
  raise 'expected bytes' unless Tsugumi.decode_bytes(rows.first.fetch('bytes')) == "\x00\xff".b
  rows.first['bytes'] = Tsugumi.bytes("\x00\x0d\xff".b)
  rows.first['readings'] = [Float::NAN, Float::INFINITY, -Float::INFINITY]
  rows.first['choice'] = {type: 'Count', value: {value: 12, default: 3, _hidden: 4}}
  raise 'expected keyword names' unless rows.first.fetch('tone') == 'delete' &&
                                        rows.first.fetch('history').first.fetch('value').fetch('default') == 5
  flags = rows.first.fetch('position').fetch('flags')
  raise 'expected an absent optional struct' unless rows.first.fetch('center').nil?
  raise 'expected a fixed array of booleans' unless flags == [true, false, false, true]
  flags.rotate!
  output = {number: {value: number.fetch('value') + 2}, rows: {rows: rows}}
  if (path = input[:file])
    File.write('ruby-output.data', File.read(path).upcase)
    output[:file] = Pathname('ruby-output.data')
    output[:original] = path
  end
  output[:directory] = input[:directory]
  if (arrival = input[:arrival])
    value = arrival.fetch('value')
    output[:arrival] = case arrival.fetch('type')
                       when 'Temperature' then {type: 'Temperature', value: {value: value.fetch('value') + 1}}
                       when 'Reading' then {type: 'Reading', value: {name: value.fetch('name')}}
                       end
  end
  if (keyword = input[:class])
    output[:switch] = {value: keyword.fetch('value') + 1}
  end
  output
end
