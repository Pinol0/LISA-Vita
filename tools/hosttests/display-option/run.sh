#!/bin/bash
# usage: run.sh RUBY SCRIPTS_DIR   patches/display_option.rb on LISA's real options menu, four launches
# (Original, no binding, Wide saved, Stretch saved).
# Mutants (must FAIL): entry appended at the end; left / right swapped; entry not drawn; camera not
# centred after a resize; saved Wide not applied when the game starts.
HERE=$(cd "$(dirname "$0")" && pwd)
RUBY=$1; S=$2
P="$(cd "$HERE/../../.." && pwd)/patches/display_option.rb"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/display-option.XXXXXX")
runs() { local rc=0 m; for m in "" no_binding boot_wide legacy_stretch; do (cd "$OUT" && PATCH=$1 $RUBY "$HERE/t_display_option.rb" "$S" $m) || rc=1; done; return $rc; }
runs "$P"
rc=$?
echo "mutation:"
mut() {
    python3 -c 'import sys; s=open(sys.argv[1]).read(); assert s.count(sys.argv[3])==1, sys.argv[3]; open(sys.argv[2],"w").write(s.replace(sys.argv[3], sys.argv[4]))' \
        "$P" "$OUT/$1.rb" "$2" "$3" || { echo "FAIL mutant $1 not applied"; rc=1; return; }
    if runs "$OUT/$1.rb" > "$OUT/$1.log" 2>&1; then echo "FAIL mutant $1 survived"; rc=1; else echo "PASS mutant $1 killed: $(grep -m1 '^FAIL' "$OUT/$1.log")"; fi
}
mut at_end "      at = at ? at + 1 : @list.index { |c| c[:symbol] == :to_title } || @list.size" "      at = @list.size"
mut swapped "      nxt = direction == :right ?" "      nxt = direction == :left ?"
mut not_drawn "      return vita_display_draw_item(index) unless @list[index][:symbol] == :vita_screen" "      return vita_display_draw_item(index)"
mut no_center "    \$game_player.center(\$game_player.x, \$game_player.y) if \$game_map && \$game_player
" ""
mut no_boot_apply "  VitaDisplayOption.apply_size
  VitaDisplayOption.log(\"installed" "  VitaDisplayOption.log(\"installed"
rm -rf "$OUT"
exit $rc
