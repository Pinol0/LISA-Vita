#!/bin/bash
# usage: run.sh RUBY SCRIPTS_DIR   (LISA's scripts extracted by setup_env.sh)
#   env JOYFUL_SCRIPTS: LISA: The Joyful's extracted scripts (optional: its launch is skipped without)
# patches/achievements.rb over several launches of LISA's Steam scripts. Mutants (must FAIL): the
# unlock reported as done without being written; the command added after Shut Down; a pending unlock
# never answered (setAchievement left to the stub).
cd "$(dirname "$0")"
RUBY=$1; S=$2
OUT=$(mktemp -d "${TMPDIR:-/tmp}/achievements.XXXXXX")
launches() {   # PATCH file -> all launches in a fresh game folder
    local p=$1 w=$OUT/game rc=0
    rm -rf "$w"; mkdir -p "$w"
    for s in before after again; do PATCH=$p $RUBY t_achievements.rb "$S" $s "$w" || rc=1; done
    PATCH=$p $RUBY t_achievements.rb "$S" nowrite "$w" || rc=1
    rm -rf "$w"; mkdir -p "$w"
    PATCH=$p $RUBY t_achievements.rb "$S" badwindow "$w" || rc=1
    rm -rf "$w"; mkdir -p "$w"
    PATCH=$p $RUBY t_achievements.rb "$S" logdir "$w" || rc=1
    if [ -n "${JOYFUL_SCRIPTS:-}" ]; then
        rm -rf "$w"; mkdir -p "$w"
        PATCH=$p $RUBY t_achievements.rb "$JOYFUL_SCRIPTS" joyful "$w" || rc=1
    fi
    return $rc
}
launches "$(cd ../../.. && pwd)/patches/achievements.rb"
rc=$?
icons() {   # ICONS file -> synthetic graphics; the game's real ones with LISA_DIR / JOYFUL_DIR
    local w=$OUT/game rc=0
    rm -rf "$w"; mkdir -p "$w"
    ICONS=$1 $RUBY t_achievements.rb "$S" icons "$w" || rc=1
    if [ -n "${LISA_DIR:-}" ]; then
        rm -rf "$w"; mkdir -p "$w"
        ICONS=$1 GAME_DIR=$LISA_DIR $RUBY t_achievements.rb "$S" icons_real "$w" || rc=1
    fi
    if [ -n "${JOYFUL_DIR:-}" ] && [ -n "${JOYFUL_SCRIPTS:-}" ]; then
        rm -rf "$w"; mkdir -p "$w"
        ICONS=$1 GAME_DIR=$JOYFUL_DIR $RUBY t_achievements.rb "$JOYFUL_SCRIPTS" icons_real "$w" || rc=1
    fi
    return $rc
}
ICONS_RB="$(cd ../../.. && pwd)/patches/achievement_icons.rb"
icons "$ICONS_RB" || rc=1
echo "mutation:"
mut() {   # name old new
    python3 -c 'import sys; s=open(sys.argv[1]).read(); assert s.count(sys.argv[3])==1, sys.argv[3]; open(sys.argv[2],"w").write(s.replace(sys.argv[3], sys.argv[4]))' \
        ../../../patches/achievements.rb "$OUT/$1.rb" "$2" "$3" || { echo "FAIL mutant $1 not applied"; rc=1; return; }
    if launches "$OUT/$1.rb" > "$OUT/$1.log" 2>&1; then echo "FAIL mutant $1 survived"; rc=1; else echo "PASS mutant $1 killed: $(grep -m1 '^FAIL' "$OUT/$1.log")"; fi
}
mut not_written "    File.open(FILE, 'a') { |f| f.puts \"#{id} #{t}\" }
" ""
mut command_last "    at = @list.index { |c| c[:symbol] == :shutdown } || @list.size" "    at = @list.size"
mut strftime "    status = at ? \"Unlocked #{VitaAchievements.date(at)}\" : 'Locked'" "    status = at ? \"Unlocked #{Time.at(at).strftime('%Y-%m-%d')}\" : 'Locked'"
mut date_month "    y = yoe + era * 400 + (m <= 2 ? 1 : 0)" "    y = yoe + era * 400"
if [ -n "${JOYFUL_SCRIPTS:-}" ]; then
    mut unknown_every_time "      log(\"unknown id #{id.inspect}\") unless @unknown[id]   # once: the game pushes it again every 60 frames" "      log(\"unknown id #{id.inspect}\")"
fi
mut dat_in_logs "  ROOT = defined?(VITA_GAME_DIR) ? VITA_GAME_DIR : File.dirname(VITA_PATCH_LOG) + '/'   # not logs/" "  ROOT = File.dirname(VITA_PATCH_LOG) + '/'"
mut stub_kept "    st.define_singleton_method(:setAchievement) { |id| VitaAchievements.unlock(id) }" "    st.define_singleton_method(:setAchievement) { |id| false }"
mutic() {   # name old new: a mutant of achievement_icons.rb
    python3 -c 'import sys; s=open(sys.argv[1]).read(); assert s.count(sys.argv[3])==1, sys.argv[3]; open(sys.argv[2],"w").write(s.replace(sys.argv[3], sys.argv[4]))' \
        "$ICONS_RB" "$OUT/$1.rb" "$2" "$3" || { echo "FAIL mutant $1 not applied"; rc=1; return; }
    if icons "$OUT/$1.rb" > "$OUT/$1.log" 2>&1; then echo "FAIL mutant $1 survived"; rc=1; else echo "PASS mutant $1 killed: $(grep -m1 '^FAIL' "$OUT/$1.log")"; fi
}
mutic sheet_kept "      sheet.dispose
" ""
mutic name_prefix "(name == a.name || name.start_with?(a.name + ' '))" "name.start_with?(a.name)"
mutic scale_fraction "    s = s.floor if s >= 1
" ""
mutic locked_bright "on ? 255 : 64" "255"
mutic hidden_shown "    if !on && VitaAchievements::INFO[id][1].empty?" "    if false"
mutic all_at_once "      @todo << i" "      draw_item(i)"
mutic popup_no_id "    id = VitaAchievements.ids.find { |k| VitaAchievements::INFO[k][0] == title }" "    id = nil"
mutic char_frame "        cw, ch, bx, by = sheet.width / 3, sheet.height / 4, 0, 0" "        cw, ch, bx, by = sheet.width / 3, sheet.height / 4, 0, sheet.height / 4"
rm -rf "$OUT"
exit $rc
