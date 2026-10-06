$calls = []
module Graphics; @f = 0; def self.frame_count; @f; end; def self.tick; @f += 1; end; end
def vita_trace(m); end
$ledger = 0
def vita_gl_ledger_dump(r); $ledger += 1; end
$held = ARGV[0] == 'R'
module Input; def self.update; end; def self.press?(k); k == :R && $held; end; def self.trigger?(k); false; end; end
class Scene_Base; def start; end; def update; end; def terminate; end; def return_scene; $calls << [:return_scene, self.class]; SceneManager.return; end; end
class Scene_Map < Scene_Base; end; class Scene_Menu < Scene_Base; end; class Scene_Battle < Scene_Base; end
class Scene_Save < Scene_Base; end; class Scene_Load < Scene_Base; end; class Scene_Gameover < Scene_Base; end; class Scene_Title < Scene_Base; end
module SceneManager
  @scene = nil
  def self.first_scene_class; Scene_Title; end
  def self.exit; $calls << :real_exit; end
  def self.call(k); $calls << [:call, k]; @scene = k.new; end
  def self.goto(k); $calls << [:goto, k]; @scene = k.new; end
  def self.return; $calls << :scene_return; @scene = Scene_Map.new; end
  def self.scene_is?(k); @scene.is_a?(k); end
  def self.scene; @scene; end
  def self.set(s); @scene = s; end
end
module DataManager; def self.setup_new_game; $calls << :new_game; end; def self.save_game(i); $calls << [:real_save, i]; true; end; end
module BattleManager; def self.setup(t, a, b); $calls << [:battle_setup, t]; end; def self.process_abort; $calls << :abort; SceneManager.return; end; end
class Actor; def recover_all; end; end
class GP; def members; [Actor.new]; end; end; $game_party = GP.new
class Interp; def running?; false; end; def clear; end; end
class GMap; attr_reader :map_id; def setup(i); @map_id = i; end; def interpreter; @i ||= Interp.new; end; def autoplay; end; end; $game_map = GMap.new
class GPlayer; def moveto(x, y); end; def refresh; end; def transfer?; false; end; def make_encounter_count; end
  def reserve_transfer(m, x, y, d); $calls << [:transfer, m, x, y]; $game_map.setup(m); end; end; $game_player = GPlayer.new
class GMsg; def busy?; false; end; def clear; end; end; $game_message = GMsg.new
VITA_SOAK_ROOT = './'
class Window_MenuCommand; def self.init_command_position; @@last_command_symbol = nil; end; def self.select_last; @@last_command_symbol; end; end
class Scene_Menu; def start; Window_MenuCommand.select_last; end; end
class Scene_Map; def call_menu; SceneManager.call(Scene_Menu); Window_MenuCommand::init_command_position; end; end
$data_troops = Array.new(40) { |i| i.zero? ? nil : Object.new }
module SceneManager; def self.call(k); $calls << [:call, k]; @scene = k.new; @pending_start = true; end; def self.pending_start; p = @pending_start; @pending_start = false; p; end; end
module BattleManager; @in_turn = false; class << self; attr_accessor :in_turn; end; def self.in_turn?; @in_turn; end; def self.aborting?; false; end; end
class Enemy; def initialize; @a=true; end; def alive?; @a; end; def die; @a=false; $calls << :die; end; end
class GTroop; def members; @m ||= [Enemy.new, Enemy.new]; end; end; $game_troop = GTroop.new
module ObjectSpace; def self.count_objects; {TOTAL: 10, FREE: 2, T_STRING: 3, T_ARRAY: 1, T_HASH: 1, T_OBJECT: 1, T_DATA: 1, T_STRUCT: 0}; end; end unless ObjectSpace.respond_to?(:count_objects)
# Victory as in the game: once every enemy is dead the battle scene returns to the map.
class Scene_Battle < Scene_Base
  def update; super; SceneManager.return if $game_troop && $game_troop.members.none?(&:alive?); end
end
module BattleManager; def self.setup(t, a, b); $calls << [:battle_setup, t]; $game_troop = GTroop.new; end; end
