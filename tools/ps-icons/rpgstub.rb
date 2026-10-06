# Minimal RPG classes so Marshal can load RPG Maker VX Ace data on the host.
class Table; attr_reader :raw; def self._load(s); t = allocate; t.instance_variable_set(:@raw, s); t; end; def _dump(d = 0); @raw; end; end
class Color < Table; end
class Tone < Table; end
module RPG
  class BaseItem; class Feature; end; end
  class UsableItem < BaseItem; class Damage; end; class Effect; end; end
  class Skill < UsableItem; end
  class Item < UsableItem; end
  class EquipItem < BaseItem; end; class Weapon < EquipItem; end; class Armor < EquipItem; end
  class Actor < BaseItem; end; class Class < BaseItem; class Learning; end; end
  class Enemy < BaseItem; class DropItem; end; class Action; end; end
  class State < BaseItem; end
  class AudioFile; end; class SE < AudioFile; end; class BGM < AudioFile; end; class BGS < AudioFile; end; class ME < AudioFile; end
  class Animation; class Frame; end; class Timing; end; end
  class Event; class Page; class Condition; end; class Graphic; end; end; end
  class EventCommand; end; class MoveRoute; end; class MoveCommand; end
  class Map; class Encounter; end; end; class MapInfo; end; class CommonEvent; end
  class Troop; class Member; end; class Page; class Condition; end; end; end
  class Tileset; end
  class System; class Vehicle; end; class Terms; end; class TestBattler; end; end
end
