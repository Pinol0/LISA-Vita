# Replaces the key letters at the start of LISA's combo skill descriptions, e.g. "(WSS) A blast of
# flaming emotion.", with the icons of the PlayStation buttons (tools/ps-icons/IconSet.png):
# "(\I[72]\I[74]\I[74]) A blast ...". Each letter gets the icon of the button skill of the fighting
# style that owns the combo (notetags <combo skill R/X/Y/Z: id> and <combo special RYY: id> of the
# parent skill), so every character keeps its own colours; W=R, A=X, S=Y, D=Z. A combo whose letters
# do not match its notetag sequence gets the notetag's sequence (reported); a combo with no owner
# keeps its letters and gets the default icons 72-75.
# Only the description strings change, in place in the original byte stream (Marshal links objects
# by index, not by offset; re-dumping with a modern Ruby would turn RGSS3's inline Floats into links).
# The result is checked: loaded again, every skill equals the original except its description.
#   ruby patch_skill_descriptions.rb IN_Skills.rvdata2 OUT_Skills.rvdata2
require_relative 'rpgstub'
src, dst = ARGV
abort 'usage: patch_skill_descriptions.rb IN OUT' unless src && dst
raw = File.binread(src)
skills = Marshal.load(raw)
iv = ->(o, n) { o.instance_variable_get(n) }
LETTER = { 'W' => 'R', 'A' => 'X', 'S' => 'Y', 'D' => 'Z' }
DEFAULT = { 'W' => 72, 'A' => 73, 'S' => 74, 'D' => 75 }
owner = {}   # combo skill id -> [button -> icon, sequence]
skills.compact.each do |p|
  note = iv.(p, :@note).to_s
  buttons = {}
  note.scan(/<combo skill ([LRXYZ]): *(\d+)>/i) { |b, id| s = skills[id.to_i]; buttons[b.upcase] = iv.(s, :@icon_index) if s }
  note.scan(/<combo special (.*?): *(\d+)>/i) { |seq, id| (owner[id.to_i] ||= []) << [buttons, seq.upcase, iv.(p, :@id)] }
end
changed = 0
problems = []
orig_desc = {}
skills.compact.each do |s|
  d = iv.(s, :@description).to_s
  m = d.match(/\A\(([WASD]+)\)/) or next
  orig_desc[iv.(s, :@id)] = d.dup
  letters = m[1].chars
  seq = letters.map { |l| LETTER[l] }.join
  all = owner[iv.(s, :@id)] || []
  owners = all.select { |_, sq, _| sq == seq }
  # the original game has a few descriptions that differ from the real sequence (Running Typhoon,
  # Muscle Call, Hell's Call): when every owning style agrees on one sequence, show that one
  buttons = (owners.first || all.first || [{}])[0]
  if owners.empty?
    seqs = all.map { |_, sq, _| sq }.uniq
    if seqs.size == 1 && seqs[0] =~ /\A[RXYZ]+\z/
      fixed = seqs[0].tr('RXYZ', 'WASD')
      problems << "#{iv.(s, :@id)} #{iv.(s, :@name).strip}: #{m[0]} corrected to (#{fixed}) as in its notetag"
      letters = fixed.chars
    else
      problems << "#{iv.(s, :@id)} #{iv.(s, :@name).strip}: #{m[0]} kept: #{seqs.empty? ? 'no style uses it' : 'styles disagree'}"
    end
  end
  icons = letters.map { |l| buttons[LETTER[l]] || DEFAULT[l] }
  new = '(' + icons.map { |i| "\\I[#{i}]" }.join + ')' + d[m[0].size..]
  new.force_encoding(d.encoding)
  s.instance_variable_set(:@description, new)
  changed += 1
  puts "#{iv.(s, :@id).to_s.rjust(4)} #{iv.(s, :@name).strip.ljust(22)} #{m[0].ljust(8)} -> #{icons.join(',')}"
end
# Marshal long (w_long) and in-place replacement of each description string, in data order.
wlong = lambda do |n|
  return "\x00".b if n == 0
  return [n + 5].pack('C') if n > 0 && n < 123
  bytes = []
  x = n
  while x > 0
    bytes << (x & 0xff)
    x >>= 8
  end
  [bytes.size].pack('C') + bytes.pack('C*')
end
out = raw.dup.force_encoding('BINARY')
pos = 0
expected = {}
skills.compact.each do |s|
  newd = iv.(s, :@description)
  oldd = orig_desc[iv.(s, :@id)] or next
  enc_old = '"'.b + wlong.(oldd.bytesize) + oldd.dup.force_encoding('BINARY')
  enc_new = '"'.b + wlong.(newd.bytesize) + newd.dup.force_encoding('BINARY')
  i = out.index(enc_old, pos) or abort "description of skill #{iv.(s, :@id)} not found in the byte stream"
  out[i, enc_old.bytesize] = enc_new
  pos = i + enc_new.bytesize
  expected[iv.(s, :@id)] = newd
end
check = Marshal.load(out)
orig = Marshal.load(raw)
orig.each_with_index do |o, k|
  c = check[k]
  next if o.nil? && c.nil?
  o.instance_variables.each do |v|
    a = o.instance_variable_get(v); b = c.instance_variable_get(v)
    if v == :@description && expected.key?(k)
      abort "skill #{k}: wrong new description" unless b == expected[k]
    elsif Marshal.dump(a) != Marshal.dump(b)
      abort "skill #{k}: #{v} changed"
    end
  end
end
File.binwrite(dst, out)
puts "changed=#{changed} bytes #{raw.bytesize} -> #{out.bytesize}; reloaded: only the descriptions differ"
puts problems.empty? ? 'all sequences match their notetags' : problems
