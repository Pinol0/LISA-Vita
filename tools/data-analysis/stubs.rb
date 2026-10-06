class Table; attr_reader :dim,:xs,:ys,:zs,:data
  def self._load(s); t=allocate; d,xs,ys,zs,n=s.unpack("l5"); t.instance_variable_set(:@xs,xs); t.instance_variable_set(:@ys,ys); t.instance_variable_set(:@zs,zs); t; end; end
class Color; attr_reader :v; def self._load(s); c=allocate; c.instance_variable_set(:@v,s.unpack("d4")); c; end; def to_s; "Color(#{@v.map{|x|x.to_i}.join(',')})"; end; alias inspect to_s; end
class Tone;  attr_reader :v; def self._load(s); c=allocate; c.instance_variable_set(:@v,s.unpack("d4")); c; end; def to_s; "Tone(#{@v.map{|x|x.to_i}.join(',')})"; end; alias inspect to_s; end
module RPG
  class BaseItem; end; class Map; end; class MapInfo; end; class Event; end; class CommonEvent; end; class Troop; end; class System; end
  class Event::Page; end; class Event::Page::Condition; end; class Event::Page::Graphic; end
  class EventCommand; attr_reader :code,:indent,:parameters; end
  class MoveRoute; attr_reader :repeat,:skippable,:wait,:list; end; class MoveCommand; attr_reader :code,:parameters; end
  class AudioFile; attr_reader :name,:volume,:pitch; def inspect; "#{self.class.name.split('::').last}(#{@name})"; end; end
  class BGM<AudioFile; end; class BGS<AudioFile; end; class SE<AudioFile; end; class ME<AudioFile; end
  class Map::Encounter; end; class Troop::Member; end; class Troop::Page; end; class Troop::Page::Condition; end
  class System::Vehicle; end; class System::Terms; end; class System::TestBattler; end
end
class Object; def iv(n); instance_variable_get("@#{n}"); end; end
module RPG
  class BaseItem; class Feature; end; end
  class UsableItem < BaseItem; class Damage; end; class Effect; end; end
  class Enemy < BaseItem; class Action; end; class DropItem; end; end
  class Actor < BaseItem; end; class Class < BaseItem; class Learning; end; end
  class Item < UsableItem; end; class Skill < UsableItem; end; class EquipItem < BaseItem; end; class Weapon < EquipItem; end; class Armor < EquipItem; end; class State < BaseItem; end
end
