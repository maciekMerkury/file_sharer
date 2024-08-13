#include <libnotify/notify.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "entry.h"
#include "message.h"
#include "notification.h"

const char directory_icon[] = "inode-directory";
const char file_icon[] = "text-x-preview";

static GMainLoop *loop;
static request_response res;

static void refuse_callback(NotifyNotification *n, const char *action,
			    gpointer user_data)
{
	GError *err;
	if (!notify_notification_close(n, &err))
		fprintf(stderr, "failed to close notification\n");

	res = rr_refuse;
}

static void accept_callback(NotifyNotification *n, const char *action,
			    gpointer user_data)
{
	GError *err;
	if (!notify_notification_close(n, &err))
		fprintf(stderr, "failed to close notification\n");

	res = rr_accept;
}

static void closed_callback(NotifyNotification *n, gpointer user_data)
{
	if (res == rr_error)
		res = rr_refuse;

	g_main_loop_quit(loop);
}

request_response request_notification(char username[],
				      char addr_str[INET_ADDRSTRLEN],
				      request_data_t *request_data)
{
	const char *icon = request_data->entry_type == et_dir ? directory_icon :
								file_icon;
	res = rr_error;
	char body[256];
	size_info size = bytes_to_size(request_data->total_file_size);
	snprintf(body, 256,
		 "Client %s (%s) wants to send you %s `%s` of size %.2lf %s",
		 username, addr_str,
		 get_entry_type_name(request_data->entry_type),
		 request_data->filename, size.size, unit(size));

	NotifyNotification *n =
		notify_notification_new("File Transfer Request", body, icon);
	notify_notification_set_timeout(n, NOTIFY_EXPIRES_NEVER);
	GVariant *hint = g_variant_new_boolean(TRUE);
	notify_notification_set_category(n, "transfer");
	notify_notification_set_hint(n, "resident", hint);

	notify_notification_add_action(n, "refuse", "Refuse",
				       NOTIFY_ACTION_CALLBACK(refuse_callback),
				       loop, NULL);
	notify_notification_add_action(n, "accept", "Accept",
				       NOTIFY_ACTION_CALLBACK(accept_callback),
				       loop, NULL);
	g_signal_connect(n, "closed", G_CALLBACK(closed_callback), loop);

	if (!notify_notification_show(n, NULL)) {
		fprintf(stderr, "failed to send notification\n");
		return -1;
	}

	g_main_loop_run(loop);
	g_variant_unref(hint);

	return res;
}

int notifications_init(const char *name)
{
	if (!notify_init(name))
		ERR_GOTO("notify init");

	loop = g_main_loop_new(NULL, FALSE);

	return 0;

error:
	return -1;
}

void notifications_deinit(void)
{
	g_main_loop_unref(loop);
	loop = NULL;
}
