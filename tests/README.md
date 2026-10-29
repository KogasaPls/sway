# Adaptive sync opt-in checks

Configure and build sway first, then run from the source checkout:

```sh
build-run meson compile -C build
build-run tests/adaptive-sync.sh
```

Requires a C compiler and linker with section garbage collection, pkg-config,
wlroots development headers, swaymsg, foot, and jq. `SWAY_BUILD_DIR`, `SWAY_BIN`,
`FOOT_BIN`, and `CC` can override the configured build, compositor, terminal,
and compiler executable. If `foot` resolves to a desktop-session launcher, use
`FOOT_BIN=/usr/bin/foot` to run it directly in the test's private runtime directory.

The C checks exercise the real auto-policy function and command, including
fullscreen visibility, default opt-out, session lock, global fullscreen, idle
frame delivery, rejection reset, and unchanged explicit output modes. Linker
section garbage collection keeps unrelated compositor code out of the test.

The IPC checks start a private pixman/headless compositor with two outputs.
They cover rules, runtime changes, validation, parent containers, movement,
reload, and IPC permission reporting. No windows appear in the running session;
cleanup terminates only test-owned processes.

Headless outputs do not support physical VRR. These tests establish policy and
command behavior, not driver acceptance, monitor transitions, or hitch-free
playback on real hardware.
