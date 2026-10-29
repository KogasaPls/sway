#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"

struct cmd_results *cmd_allow_adaptive_sync(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "allow_adaptive_sync",
			EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}

	struct sway_container *con = config->handler_context.container;
	if (!con || !con->view) {
		return cmd_results_new(CMD_INVALID,
				"Adaptive sync can only be allowed on views");
	}

	struct sway_view *view = con->view;
	bool allowed;
	if (strcasecmp(argv[0], "yes") == 0) {
		allowed = true;
	} else if (strcasecmp(argv[0], "no") == 0) {
		allowed = false;
	} else if (strcasecmp(argv[0], "toggle") == 0) {
		allowed = !view->allow_adaptive_sync;
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected allow_adaptive_sync yes|no|toggle");
	}

	if (view->allow_adaptive_sync != allowed) {
		view->allow_adaptive_sync = allowed;
		// A global fullscreen view can affect every output. Schedule a frame
		// even for static content so the new policy reaches the next commit.
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			if (output->adaptive_sync_auto) {
				output->adaptive_sync_auto_rejected = false;
				wlr_output_schedule_frame(output->wlr_output);
			}
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
