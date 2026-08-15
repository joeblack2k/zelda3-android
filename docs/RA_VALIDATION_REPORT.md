# RetroAchievements Validation Report

Date: August 15, 2026

Verification-removal follow-up: August 15, 2026

## Scope

This draft integrates private RetroAchievements support into the Android port.
The local canonical-ROM ownership check has been removed: the ROM is still
read once on the device to build the port assets, but it is not verified,
stored, or required as a RetroAchievements gate.

- Branch: `feature/retroachievements-private`
- Current source commit: `40c9728`
- Base commit: `6fb832027a6e9b4812244e90dcc039303bd9e15b`
- Base branch: `samyost1/zelda3-android:dual-screen`
- rcheevos: official `12.4.0` snapshot at
  `2ad0b8672f68a48148620164510b963039e49eb1`
- Target device: AYN Thor, Android 13, arm64-v8a

The host and test device clocks displayed July 26 during the final run, but
both clocks were one day ahead. The actual validation date is July 25, 2026.

## Implemented

- RetroAchievements does not depend on a local canonical-ROM verification.
- The native client still requests game 355 with hash
  `608c22b8ff930c62dc2de54bcd6eba72` to select the correct RA achievement
  set; this is set identity, not local ROM ownership verification.
- Private INI configuration with token login and password-login fallback.
- Token persistence in the app-private data directory with restrictive file
  permissions.
- Casual mode is enabled by default when credentials are available; hardcore is
  always disabled.
- Native rcheevos client lifecycle, SNES memory mapping, logical-frame
  processing, and asynchronous HTTPS request handling.
- Pause/resume handling, disconnect/reconnect plumbing, and logout cleanup.
- RetroAchievements progress embedded in save states with version and checksum
  validation while preserving legacy save-state compatibility.
- Casual-mode integrity guards that reject replay/direct-cheat paths.
- Session-only cheats remain available without shutting down the RA client;
  hardcore stays disabled when cheats are used.
- Dual-screen companion panel with connection state, rich presence,
  achievements, scrolling, and logout controls. The old verification control
  and `NO VERIFIED GAME` status are gone.
- Debug-only `RA_TEST` and `RA_DUMP` broadcasts for deterministic validation.
  Their action strings and diagnostic method are absent from the release APK.
- Existing Android ABI declarations remain
  `armeabi-v7a`, `arm64-v8a`, `x86`, and `x86_64`.

## Automated Validation

The current cleanup build used:

```sh
./gradlew :app:assembleDebug --no-daemon
```

The native build used the locally installed working NDK 23.2. After the build,
`local.properties` was restored to the normal NDK 25.2 setting.

- RetroAchievements JVM tests: 3 passed.
- Native/static assertions: passed.
- Debug APK assembled successfully.
- `git diff --check`: clean.

Final artifacts:

| Artifact | SHA-256 |
| --- | --- |
| `app/build/outputs/apk/debug/app-debug.apk` | `1cdc59f2b94170649f732670c651ac13c5107ade87889dd2692e9399531965c2` |

The earlier release APK was scanned for `RA_TEST`, `RA_DUMP`, and
`uiModelDiagnostics`; no matches were found.

## Physical Device Validation

The current `40c9728` APK was not installed during this follow-up. At the
final deployment attempt, `adb devices -l` returned no devices and
`adb mdns services` returned no Wi-Fi ADB service. No gameplay or runtime test
was performed, as requested.

The detailed runtime evidence below belongs to the earlier installed build and
is retained as historical context; it does not certify the current APK.

The previous debug APK was installed on the AYN Thor with:

```sh
adb -s 6b0af897 install -r -t \
  app/build/outputs/apk/debug/app-debug.apk
```

That installation succeeded. The `-t` flag is required because this project's debug
APK is marked test-only; plain `adb install -r` is rejected by Android.

After a force-stop and fresh launch of that previous build, the debug snapshot
reported:

```text
enabled=1
mode=spectator
lifecycle=resumed
state=5
login=authenticated
game=355
console=3
hash=match
expected_hash=608c22b8ff930c62dc2de54bcd6eba72
hardcore=0
spectator=1
summary=core:109/unlocked:0/unsupported:0
rp_supported=1
rp=Getting ready to adventure
invalid_reads=0
http=2/2/0/0/0
ua=Zelda3Android/0.1.0 (Android 13; AYN Thor) rcheevos/12.4
```

No account name or credential is included in this report.

Additional physical checks:

| Check | Result |
| --- | --- |
| RA game-set identity | Passed: game 355, SNES console 3 |
| Private token authentication | Passed |
| Spectator mode and hardcore disabled | Passed |
| Rich Presence | Passed |
| Achievement list | Passed: 109 core achievements, zero unsupported |
| Invalid memory reads | Passed: zero |
| Gameplay on primary display | Passed |
| Map/companion UI on physical second display | Passed |
| Long achievement title/description rendering | Passed |
| Achievement list scrolling | Passed |
| Activity pause | Passed: logical frame counter stayed at 8210 for 5 seconds |
| Normal frame processing | Passed: 221 logical frames in approximately 3.7 seconds |
| Turbo frame processing | Passed: 2993 logical frames in approximately 3.8 seconds |
| Save-state progress footer | Passed: `ZRAP` footer present in the generated save |
| Save-state restore | Passed: runtime logged `progress restore=ok` |
| Setup refresh path | Passed in the previous build; the current build has no verification-only setup action |
| Natural Casual achievement unlock | Passed: `Fighter` (ID 944) triggered and was confirmed by the server |
| Wi-Fi disable/enable recovery | Partially observed; see limitations |

The turbo test demonstrates that RetroAchievements processing follows logical
game frames before render skipping, rather than only rendered frames.

The Casual unlock follow-up restored a state immediately before receiving the
Fighter's Sword and Shield, then advanced the original dialogue through normal
game input. The live memory transition was:

```text
scene bytes 0x0AA1..0x0AA4: 01 10 4D 0A
sword 0xF359: 0 -> 1
shield 0xF35A: 0 -> 1
```

rcheevos emitted `RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED`, the local unlocked
count increased from 2 to 3, Rich Presence changed to include
`Fighter's Sword`, all HTTP requests completed without error, and a separate
server query confirmed achievement ID 944 in the user's Casual unlock list.
No memory patch, replay, synthetic unlock, or direct award request was used.

## Security and Privacy Checks

- The ROM, private INI file, APKs, and save states are ignored by Git.
- The actual local password and token values were compared against tracked
  files; neither value was found.
- No private configuration file, ROM, APK, or newly generated save state is
  tracked by this branch.
- Existing upstream reference saves and assets were not modified.
- Logout uses a local tombstone so stale external credentials cannot silently
  reactivate the client if cleanup fails. A successful cleanup removes the
  tombstone and permits an intentional later login.

## Known Limitations

- A deterministic server reconnect event was not produced in spectator mode:
  Wi-Fi was disabled for 120 seconds and restored successfully, but the idle
  spectator client made no request while offline. Authentication remained
  valid after Android reported Wi-Fi connected and validated. The asynchronous
  reconnect path is implemented, but a real disconnected/reconnected event was
  not claimed as observed.
- The Apple-silicon validation host can execute the local NDK only for
  `arm64-v8a`. Source-level ABI declarations still preserve all four upstream
  ABIs; a complete multi-ABI release build requires a compatible Intel/Linux
  Android build host.
- The current follow-up rebuilt the debug APK only; the release APK was not rebuilt.
- Incremental builds inside the macOS synchronized Documents tree can create
  duplicate generated `* 2.class` files. A clean build is reliable and was used
  for all final artifacts.
- The NDK emits non-fatal `fcntl(): Bad file descriptor` messages and a
  deprecation warning for `ndk.dir`. The debug build also retains the existing
  unoptimized Opus warning. None caused a failed task.
- The port still needs a compatible ROM once to build `zelda3_assets.dat`.
  That asset-source requirement is separate from the removed
  RetroAchievements verification gate.

## Conclusion

The private RetroAchievements integration now builds without a local ROM
verification gate, defaults to Casual mode, and keeps the cheat path compatible
with achievements. The current debug APK is built and the draft PR is updated.
Installation of this exact APK on the Thor remains pending because ADB was not
connected at the end of the run; runtime claims for this build are therefore
intentionally not made.
