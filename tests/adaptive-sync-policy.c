#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

struct sway_server server;
struct sway_root *root;
struct sway_config *config;

static void expect(struct sway_output *output, bool wanted, const char *name) {
	assert(output_auto_adaptive_sync_wanted(output) == wanted);
	printf("PASS: %s\n", name);
}

static unsigned int frames;

static void handle_frame(struct wl_listener *listener, void *data) {
	frames++;
}

static void test_command_schedules_frames(void) {
	struct sway_view view = {0};
	struct sway_container con = { .view = &view };
	struct sway_config test_config = { .handler_context.container = &con };
	config = &test_config;
	struct wl_event_loop *loop = wl_event_loop_create();
	assert(loop);
	struct wl_listener listeners[3] = {0};
	struct wlr_output wlr_outputs[3] = {0};
	struct sway_output outputs[3] = {0};
	void *items[] = { &outputs[0], &outputs[1], &outputs[2] };
	list_t output_list = { .length = 3, .items = items };
	struct sway_root test_root = { .outputs = &output_list };
	root = &test_root;
	for (size_t i = 0; i < 3; ++i) {
		wlr_outputs[i].enabled = true;
		wlr_outputs[i].event_loop = loop;
		wl_signal_init(&wlr_outputs[i].events.frame);
		wl_signal_init(&wlr_outputs[i].events.needs_frame);
		listeners[i].notify = handle_frame;
		wl_signal_add(&wlr_outputs[i].events.frame, &listeners[i]);
		outputs[i].wlr_output = &wlr_outputs[i];
		outputs[i].adaptive_sync_auto = i < 2;
		outputs[i].adaptive_sync_auto_rejected = true;
	}

	char *yes[] = { "yes" };
	struct cmd_results *result = cmd_allow_adaptive_sync(1, yes);
	assert(result->status == CMD_SUCCESS && view.allow_adaptive_sync);
	free_cmd_results(result);
	assert(wlr_outputs[0].needs_frame && wlr_outputs[1].needs_frame);
	assert(!wlr_outputs[2].needs_frame);
	wl_event_loop_dispatch_idle(loop);
	assert(frames == 2);
	assert(!outputs[0].adaptive_sync_auto_rejected);
	assert(!outputs[1].adaptive_sync_auto_rejected);
	assert(outputs[2].adaptive_sync_auto_rejected);
	puts("PASS: opt-in schedules idle auto outputs and clears rejected tests");
	puts("PASS: a view change covers global fullscreen on multiple outputs");
	puts("PASS: explicit on/off outputs are not rescheduled or reset");

	wlr_outputs[0].needs_frame = false;
	result = cmd_allow_adaptive_sync(1, yes);
	assert(result->status == CMD_SUCCESS && !wlr_outputs[0].needs_frame);
	free_cmd_results(result);
	puts("PASS: repeating the current opt-in does not schedule extra frames");

	char *no[] = { "no" };
	result = cmd_allow_adaptive_sync(1, no);
	assert(result->status == CMD_SUCCESS && !view.allow_adaptive_sync);
	assert(wlr_outputs[0].needs_frame);
	free_cmd_results(result);
	wl_event_loop_dispatch_idle(loop);
	assert(frames == 4);
	puts("PASS: opt-out schedules a frame even with static fullscreen content");
	wl_event_loop_destroy(loop);
}

int main(void) {
	struct sway_root test_root = {0};
	root = &test_root;
	struct sway_session_lock lock = {0};
	struct sway_view opted = { .allow_adaptive_sync = true };
	struct sway_view unopted = {0};
	struct sway_container fullscreen = { .view = &unopted };
	struct sway_container other_fullscreen = { .view = &unopted };
	struct sway_container group = {0};
	struct sway_workspace workspace = {0};
	struct sway_workspace other_workspace = { .current.fullscreen = &other_fullscreen };
	struct sway_output output = {0};
	struct sway_output other_output = { .current.active_workspace = &other_workspace };

	expect(&output, false, "no active workspace does not request VRR");
	output.current.active_workspace = &workspace;
	expect(&output, false, "no fullscreen view does not request VRR");
	workspace.current.fullscreen = &fullscreen;
	expect(&output, false, "fullscreen windows default to opting out of VRR");
	fullscreen.view = &opted;
	expect(&output, true, "opted-in fullscreen view requests VRR");
	expect(&other_output, false, "another output's unopted view stays fixed refresh");
	server.session_lock.lock = &lock;
	expect(&output, false, "session lock overrides an opted-in fullscreen view");
	server.session_lock.lock = NULL;
	expect(&output, true, "unlock restores the fullscreen view's opt-in");
	workspace.current.fullscreen = NULL;
	expect(&output, false, "leaving fullscreen or its workspace stops requesting VRR");
	workspace.current.fullscreen = &fullscreen;
	opted.allow_adaptive_sync = false;
	expect(&output, false, "revoking opt-in stops requesting VRR while fullscreen");
	opted.allow_adaptive_sync = true;
	expect(&output, true, "restoring opt-in requests VRR while already fullscreen");
	root->fullscreen_global = &fullscreen;
	expect(&output, true, "opted-in global fullscreen requests VRR");
	expect(&other_output, true, "global fullscreen opt-in applies on each output");
	root->fullscreen_global = &other_fullscreen;
	expect(&output, false, "unopted global fullscreen overrides an opted-in workspace");
	root->fullscreen_global = &group;
	expect(&output, false, "a global fullscreen group is not an opted-in view");
	root->fullscreen_global = NULL;
	workspace.current.fullscreen = &group;
	expect(&output, false, "a workspace fullscreen group is not an opted-in view");
	test_command_schedules_frames();
	return 0;
}
