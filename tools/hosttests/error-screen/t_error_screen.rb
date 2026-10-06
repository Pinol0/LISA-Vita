# Host test for the MKXP_VITA_ERROR_SCREEN Ruby (extracted from src/main.cpp): fake Graphics, Bitmap,
# Sprite, Viewport, Input. Checks: the text is drawn (class, message, backtrace, quit hint), the screen
# ignores X/O for 60 frames and returns true on the first trigger after; the frozen/faded screen is
# restored; qa.log gets RUBY_ERROR; if Graphics.update raises, it returns false (C fallback waits).
#   ruby t_error_screen.rb SCRIPT
SRC = File.read(ARGV[0])
class Color; def initialize(*a); end; end
class Rect; attr_reader :width, :height; def initialize(w, h); @width = w; @height = h; end; end
class Font; attr_accessor :size, :bold, :italic, :shadow, :outline, :color; end
class Bitmap
  attr_reader :width, :height, :font, :texts
  def initialize(w, h); @width = w; @height = h; @font = Font.new; @texts = []; end
  def rect; Rect.new(@width, @height); end
  def fill_rect(*a); end
  def text_size(s); Rect.new(s.size * 8, 16); end
  def draw_text(x, y, w, h, s); raise 'wide' if s.size * 8 > w; @texts << [y, s]; end
end
class Viewport; attr_accessor :z; def initialize(*a); end; end
class Sprite; attr_accessor :bitmap; def initialize(vp); $sprite = self; end; end
module Graphics
  class << self; attr_accessor :frames, :fail, :frozen, :brightness; end
  def self.width; 544; end; def self.height; 416; end
  def self.transition(d); self.frozen = false; end
  def self.update; raise 'render broken' if fail; self.frames += 1; end
end
module Input
  def self.update; end
  def self.trigger?(k); Graphics.frames >= $press_at && k == :C; end
end
fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
def boom; raise NoMethodError, "undefined method `x' for nil:NilClass\nsecond line"; end
begin; boom; rescue Exception => e; $vita_error = e; end
File.delete('qa.log') if File.exist?('qa.log')
Graphics.frames = 0; Graphics.frozen = true; Graphics.brightness = 0; $press_at = 5
r = eval(SRC)
t = $sprite.bitmap.texts.map(&:last)
check.(r == true, 'returns true after X/O')
check.(Graphics.frames == 61, "X/O ignored for the first 60 frames (closed at frame #{Graphics.frames})")
check.(!Graphics.frozen && Graphics.brightness == 255, 'frozen / faded screen restored')
check.(t.include?('NoMethodError') && t.any? { |x| x.include?("undefined method `x'") } && t.include?('second line'), 'class and message lines drawn')
check.(t.any? { |x| x.include?('t_error_screen.rb') && x.include?('boom') }, 'backtrace drawn')
check.(t.last.include?('Press X or O'), 'quit hint drawn last')
check.(File.read('qa.log').start_with?("RUBY_ERROR NoMethodError: undefined method `x' for nil:NilClass second line\n  @ "), 'qa.log RUBY_ERROR line + backtrace')
long = 'y' * 300
begin; raise RuntimeError, long; rescue => e2; $vita_error = e2; end
Graphics.frames = 0; $press_at = 70
eval(SRC)
check.($sprite.bitmap.texts.map(&:last).count { |x| x =~ /\Ay+\z/ } == 5, 'a long message is wrapped to the screen width')
Graphics.fail = true
check.(eval(SRC) == false && File.read('qa.log').include?('ERROR_SCREEN_FAILED RuntimeError: render broken'), 'Graphics.update raising -> false (fallback) + logged')
File.delete('qa.log')
puts "fails=#{fails}"; exit(fails == 0 ? 0 : 1)
