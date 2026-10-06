require_relative 'stubs'
File.delete('./soak.log') rescue nil
eval(File.read(ARGV[1]), TOPLEVEL_BINDING, 'soak.rb', 1)
SceneManager.set(SceneManager.first_scene_class.new)
err = nil
begin
  12000.times do
    Graphics.tick
    s = SceneManager.scene
    s.update
    SceneManager.scene.start if SceneManager.pending_start   # like SceneManager.run: start the new scene
    s.terminate if s.is_a?(Scene_Battle) && !SceneManager.scene.equal?(s)
  end
rescue Exception => e
  err = "#{e.class}: #{e.message}"
end
puts "#{ARGV[1]}: #{err ? 'FAILED ' + err : 'OK'} menus=#{File.read('./soak.log').scan('MENU_RETURN').size} battles=#{File.read('./soak.log').scan('BATTLE_END').size}"
