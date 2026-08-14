#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
main="$root/app/jni/src/src/main.c"
rtl="$root/app/jni/src/src/zelda_rtl.c"
jni="$root/app/jni/src/src/platform/android/ra_client_jni.c"
client="$root/app/jni/src/src/ra_client_zelda3.c"
header="$root/app/jni/src/src/ra_client_zelda3.h"
second_screen="$root/app/jni/src/src/second_screen.c"
state="$root/app/jni/src/src/ra_state.c"
state_test="$root/app/jni/src/tests/ra_state_test.c"

awk '
  /bool is_replay = ZeldaRunFrame\(inputs\);/ { seen_frame = NR }
  seen_frame && /RaClientZelda3_DoFrame\(\);/ { seen_do_frame = NR }
  seen_do_frame && /frameCtr\+\+;/ { seen_counter = NR; exit }
  END { exit !(seen_frame && seen_do_frame && seen_counter &&
               seen_frame < seen_do_frame && seen_do_frame < seen_counter) }
' "$main"
grep -q 'if (g_paused) {' "$main"
grep -q 'RaClientZelda3_Idle();' "$main"
grep -q 'RaClientZelda3_IsCasualIntegrityEnabled()' "$main"
grep -q 'cmd == kSaveLoad_Replay && RaClientZelda3_IsCasualIntegrityEnabled()' "$rtl"
grep -q 'ZeldaStopReplayForIntegrity' "$rtl"
grep -q 'RaStateDeserialize(payload, payload_size)' "$rtl"
! grep -q 'g_client' "$jni"
grep -q 'rc_client_idle(g_client);' "$client"
grep -q 'rc_client_progress_size(g_client)' "$client"
grep -q 'rc_client_create_achievement_list' "$client"
grep -q 'RaStateLoadFooter(rwops);' "$rtl"
grep -q 'static void RaClientZelda3_ClearPendingProgress' "$client"
awk '
  /static void RaClientZelda3_ResetRuntimeState\(void\)/ { inside = 1; next }
  inside && /^}/ { exit bad }
  inside && /g_pending_progress/ { bad = 1 }
  END { exit bad }
' "$client"
grep -q 'RaClientZelda3_ApplyPendingProgress();' "$client"
grep -q 'RaClientZelda3_ClearPendingProgress();' "$client"
grep -q 'EXTRA_REFRESH_RETROACHIEVEMENTS' "$root/app/src/main/java/com/dishii/zelda3/SetupActivity.java"
grep -q 'protected void onNewIntent(Intent intent)' "$root/app/src/main/java/com/dishii/zelda3/MainActivity.java"
grep -q 'RECEIVER_EXPORTED' "$root/app/src/main/java/com/dishii/zelda3/MainActivity.java"
grep -q 'x >= raListLeft && x <= raListRight' "$root/app/src/main/java/com/dishii/zelda3/MinimapView.java"
grep -Fq "writer.buffer[record_start] = '\\0';" "$client"
grep -Fq "RaClientZelda3_AppendUiChar(&writer, 'M')" "$client"
grep -Fq "RaClientZelda3_AppendUiChar(&writer, 'A')" "$client"
grep -q 'JniNewStringFromUtf8' "$jni"

# Cheat game-thread action invariants.
awk '
  /void SecondScreen_RunFrameHook\(void\)/ { in_hook = 1 }
  in_hook {
    line = $0
    opens = gsub(/\{/, "", line)
    closes = gsub(/\}/, "", line)
    if (/bool in_gameplay = .*submodule_index == 0;/) gameplay = NR
    if (gameplay && !grant && /if \(in_gameplay && .*give100_rupees.*give10_bombs.*\{/) {
      grant = NR
      grant_depth = depth + 1
    }
    if (/g_pending_give100_rupees = 0;/) {
      clear100_count++
      clear100 = NR
      if (!grant || grant_end || depth < grant_depth || !give100_depth || depth < give100_depth)
        bad = 1
    }
    if (/g_pending_give10_bombs = 0;/) {
      clear10_count++
      clear10 = NR
      if (!grant || grant_end || depth < grant_depth || !give10_depth || depth < give10_depth)
        bad = 1
    }
    if (grant && !grant_end && depth >= grant_depth) {
      if (/if \(give100_rupees\) \{/)
        give100_depth = depth + 1
      if (/if \(give10_bombs\) \{/)
        give10_depth = depth + 1
      if (/link_rupees_goal =/) {
        if (!clear100 || NR <= clear100) bad = 1
        else write100 = NR
      }
      if (/link_item_bombs =/) {
        if (!clear10 || NR <= clear10) bad = 1
        else write10 = NR
      }
      if (depth == grant_depth && closes > opens) grant_end = NR
    }
    depth += opens - closes
  }
  END {
    exit !(gameplay && grant && grant_end &&
           clear100_count == 1 && clear10_count == 1 &&
           clear100 > grant && clear10 > grant &&
           clear100 < grant_end && clear10 < grant_end &&
           write100 > clear100 && write10 > clear10 && !bad)
  }
' "$second_screen"
! grep -Eq 'cheat_tainted|g_cheat_tainted|RaClientZelda3_TaintForCheat' "$client"
! grep -Eq 'cheat_tainted|g_cheat_tainted|RaClientZelda3_TaintForCheat' "$header"
! grep -Eq 'cheat_tainted|g_cheat_tainted|RaClientZelda3_TaintForCheat' "$second_screen"

tmp=${TMPDIR:-/tmp}/zelda3-ra-state-$$
trap 'rm -f "$tmp"' EXIT
cc -std=c99 -Wall -Werror -I"$root/app/jni/src/src" "$state_test" "$state" -o "$tmp"
"$tmp"

printf '%s\n' 'RA native static assertions passed'
