def show_values(fd)
  raise 'cancelled during the call' if Thread.new { Tsugumi.cancelled }.value
  raise 'missing deadline' unless Tsugumi.remaining_ms == 500

  Tsugumi.display("résumé\n\"")
  Tsugumi.display(Tsugumi::Markdown.new('**text**'), record: true)
  Tsugumi.display(2.5)
  Tsugumi.display(Tsugumi::Table.new(['n'], [[1]]))
  binary = "\x00\x0d\xff".b
  Tsugumi.display(Tsugumi::Image.new(binary, 'image/png', 'sample'))
  Tsugumi.display(Tsugumi::Points.new([1.0], [1.0], [1.0], [0x123456]))
  Tsugumi.display(Tsugumi::LineChart.new([{id: 'id', x: [2.0], y: [2.0], label: 'line'}]))
  Tsugumi.display(Tsugumi::Plotly.new({data: []}))
  Tsugumi.display(Tsugumi::VegaLite.new({mark: 'point'}))
  Tsugumi.display(Tsugumi::View.new('custom', {key: 1}, {'sample' => binary}), record: true)

  Thread.new do
    IO.for_fd(fd, autoclose: false).read(1)
    raise 'expected cancelled and zero time outside the call' unless Tsugumi.cancelled && Tsugumi.remaining_ms.zero?
    Tsugumi.display('outside')
  end
end

def main(input)
  @calls = (@calls || 0) + 1
  case input.params.fetch('mode')
  when 1 then show_values(input.params.fetch('fd'))
  when 2 then raise 'interpreter state was lost' unless @calls > 1
  when 3 then raise Tsugumi::Failure.new(:cancelled, 'explicit failure', cell: 'fit')
  when 4 then raise 'standard exception'
  when 5 then exit 2
  when 6 then return {}
  when 7 then return {number: {value: 2**64}, rows: input[:rows]}
  end

  rows = input.rows.fetch('rows')
  raise 'expected non-finite tokens' unless rows.first.fetch('readings') == %w[NaN Infinity -Infinity]
  raise 'expected bytes' unless Tsugumi.decode_bytes(rows.first.fetch('bytes')) == "\x00\xff".b
  rows.first['bytes'] = Tsugumi.bytes("\x00\x0d\xff".b)
  rows.first['readings'] = [Float::NAN, Float::INFINITY, -Float::INFINITY]
  rows.first['choice'] = {type: 'Count', value: {value: 12}}
  output = {number: {value: input.number.fetch('value') + 2}, rows: {rows: rows}}
  if (path = input.path(:file))
    File.write('ruby-output.data', File.read(path).upcase)
    output[:file] = {value: 'ruby-output.data'}
    output[:original] = {value: path}
  end
  output
end
