# Prints JSON with the tile layers and tileset images of the given maps (for tools/sky_check.py).
#   RGSS_DATA=.../Data/ ruby map_layers.rb ID...
require_relative 'stubs'
require 'json'
module RPG; class Tileset; end; end
class Table
  def self._load(s)
    t = allocate
    _, xs, ys, zs, n = s.unpack('l5')
    t.instance_variable_set(:@d, [xs, ys, zs, s[20, n * 2].unpack('s*')])
    t
  end
  def dims; @d; end
end
data = ENV.fetch('RGSS_DATA')
sets = Marshal.load(File.binread(File.join(data, 'Tilesets.rvdata2')))
out = {}
ARGV.map(&:to_i).each do |id|
  m = Marshal.load(File.binread(File.join(data, format('Map%03d.rvdata2', id))))
  xs, ys, zs, cells = m.iv(:data).dims
  out[id] = { 'w' => xs, 'h' => ys, 'layers' => zs, 'cells' => cells, 'tileset' => sets[m.iv(:tileset_id)].iv(:tileset_names) }
end
puts JSON.generate(out)
