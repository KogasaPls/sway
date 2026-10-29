#!/usr/bin/env bash
# Run with build-run: compiles the real policy and drives an isolated headless sway.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${SWAY_BUILD_DIR:-$repo/build}
sway_bin=${SWAY_BIN:-$build/sway/sway}
foot_bin=${FOOT_BIN:-foot}
work=$(mktemp -d /tmp/sway-optin.XXXXXX)
sway_pid=
clients=()
cleanup() {
	local pid
	for pid in "${clients[@]}"; do kill "$pid" 2>/dev/null || true; done
	if [[ -n $sway_pid ]]; then kill "$sway_pid" 2>/dev/null || true; fi
	for pid in "${clients[@]}"; do wait "$pid" 2>/dev/null || true; done
	if [[ -n $sway_pid ]]; then wait "$sway_pid" 2>/dev/null || true; fi
	rm -rf "$work"
}
trap cleanup EXIT

# Section GC isolates the actual policy function without mocking its logic or
# linking the compositor's main loop. Headers come from the configured build.
read -ra cflags <<< "$(pkg-config --cflags wlroots-0.21 pangocairo json-c libinput)"
read -ra ldflags <<< "$(pkg-config --libs wlroots-0.21 wayland-server)"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -DWLR_USE_UNSTABLE \
	-ffunction-sections -fdata-sections -I"$repo/include" \
	-I"$build/include" -I"$build/protocols" "${cflags[@]}" \
	"$repo/tests/adaptive-sync-policy.c" "$repo/sway/desktop/output.c" \
	"$repo/sway/commands/allow_adaptive_sync.c" "$repo/sway/commands.c" \
	"$repo/common/stringop.c" "$repo/common/log.c" \
	"${ldflags[@]}" -Wl,--gc-sections -o "$work/policy"
"$work/policy"

export XDG_RUNTIME_DIR=$work
unset SWAYSOCK WAYLAND_DISPLAY DISPLAY
cat > "$work/config" <<'CONF'
xwayland disable
font monospace 10
focus_follows_mouse no
output * adaptive_sync auto
for_window [app_id="^vrr-rule$"] allow_adaptive_sync yes
CONF
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=2 \
	WLR_LIBINPUT_NO_DEVICES=1 "$sway_bin" -c "$work/config" > "$work/sway.log" 2>&1 &
sway_pid=$!
sock=$work/sway-ipc.$(id -u).$sway_pid.sock
for ((i=0; i<100; i++)); do
	[[ -S $sock ]] && break
	sleep 0.05
done
if [[ ! -S $sock ]]; then cat "$work/sway.log"; exit 1; fi
for path in "$work"/wayland-*; do
	if [[ -S $path ]]; then export WAYLAND_DISPLAY=${path##*/}; break; fi
done
: "${WAYLAND_DISPLAY:?headless Wayland socket missing}"
ipc() { swaymsg -s "$sock" -r "$@"; }
command_ok() { ipc "$@" | jq -e 'all(.[]; .success)' > /dev/null; }
view_is() {
	ipc -t get_tree | jq -e --arg app "$1" \
		"[.. | objects | select(.app_id? == \$app)] | length == 1 and (.[0] | $2)" > /dev/null
}
expect_view() {
	if ! view_is "$1" "$2"; then
		printf 'FAIL: %s\n' "$3" >&2
		exit 1
	fi
	printf 'PASS: %s\n' "$3"
}
new_window() {
	"$foot_bin" --app-id="$1" --title="$1" -e sleep 3600 > "$work/$1.log" 2>&1 &
	clients+=("$!")
	for ((i=0; i<100; i++)); do
		if view_is "$1" 'true'; then return; fi
		sleep 0.05
	done
	cat "$work/$1.log" >&2
	exit 1
}
rejected() {
	local result
	result=$(ipc "$1") || true
	if ! jq -e 'length > 0 and all(.[]; .success == false)' <<< "$result" > /dev/null; then
		printf 'FAIL: command should be rejected: %s\n' "$1" >&2
		exit 1
	fi
}

new_window vrr-plain
expect_view vrr-plain '.allow_adaptive_sync == false' 'new windows default to opting out'
command_ok '[app_id="^vrr-plain$"] fullscreen enable'
expect_view vrr-plain '.fullscreen_mode == 1 and .allow_adaptive_sync == false' 'fullscreen does not opt in'
command_ok '[app_id="^vrr-plain$"] allow_adaptive_sync yes'
expect_view vrr-plain '.allow_adaptive_sync == true' 'runtime opt-in on a fullscreen window'
for cmd in 'allow_adaptive_sync' 'allow_adaptive_sync invalid' 'allow_adaptive_sync yes extra'; do
	rejected "[app_id=\"^vrr-plain$\"] $cmd"
	expect_view vrr-plain '.allow_adaptive_sync == true' 'invalid input preserves the opt-in'
done
command_ok '[app_id="^vrr-plain$"] allow_adaptive_sync toggle'
expect_view vrr-plain '.allow_adaptive_sync == false' 'toggle revokes opt-in'
command_ok '[app_id="^vrr-plain$"] allow_adaptive_sync toggle'
expect_view vrr-plain '.allow_adaptive_sync == true' 'toggle restores opt-in'
command_ok '[app_id="^vrr-plain$"] allow_adaptive_sync no'
expect_view vrr-plain '.allow_adaptive_sync == false' 'explicit runtime opt-out'
command_ok '[app_id="^vrr-plain$"] fullscreen disable'
new_window vrr-rule
expect_view vrr-rule '.allow_adaptive_sync == true' 'for_window opts matching windows in'
expect_view vrr-plain '.allow_adaptive_sync == false' 'for_window leaves nonmatching windows opted out'
command_ok '[app_id="^vrr-rule$"] fullscreen enable'
command_ok '[app_id="^vrr-rule$"] move to output HEADLESS-2'
expect_view vrr-rule '.allow_adaptive_sync == true' 'opt-in follows the view to another output'
command_ok '[app_id="^vrr-rule$"] fullscreen disable'
command_ok '[app_id="^vrr-plain$"] focus; focus parent'
rejected 'allow_adaptive_sync yes'
expect_view vrr-plain '.allow_adaptive_sync == false' 'a parent container cannot opt in its children'
command_ok 'reload'
expect_view vrr-rule '.allow_adaptive_sync == true' 'reload retains the matching opt-in rule'
new_window vrr-new
expect_view vrr-new '.allow_adaptive_sync == false' 'later windows remain opted out'
kill -0 "$sway_pid"
printf 'PASS: isolated compositor survived all opt-in commands\n'
