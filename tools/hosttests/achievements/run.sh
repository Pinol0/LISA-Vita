#!/bin/bash
# usage: run.sh RUBY SCRIPTS_DIR   (LISA's scripts extracted by setup_env.sh)
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
    return $rc
}
launches "$(cd ../../.. && pwd)/patches/achievements.rb"
rc=$?
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
mut stub_kept "    st.define_singleton_method(:setAchievement) { |id| VitaAchievements.unlock(id) }" "    st.define_singleton_method(:setAchievement) { |id| false }"
rm -rf "$OUT"
exit $rc
