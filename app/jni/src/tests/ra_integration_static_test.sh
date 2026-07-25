#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
main="$root/app/jni/src/src/main.c"
rtl="$root/app/jni/src/src/zelda_rtl.c"
jni="$root/app/jni/src/src/platform/android/ra_client_jni.c"
client="$root/app/jni/src/src/ra_client_zelda3.c"

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
grep -q 'RaClientZelda3_Reset();' "$rtl"
! grep -q 'g_client' "$jni"
grep -q 'rc_client_idle(g_client);' "$client"

printf '%s\n' 'RA native static assertions passed: 5'
