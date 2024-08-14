#include <libnotify/notify.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "entry.h"
#include "notification.h"

#define GERROR(error)                                                      \
	(fprintf(stderr,                                                   \
		 "%s:%d: %s: gerror encountered\n"                         \
		 "domain: %u, code: %d\nmessage:%s\n",                     \
		 __FILE__, __LINE__, __func__, error->domain, error->code, \
		 error->message),                                          \
	 g_error_free(error))

const char directory_icon[] = "inode-directory";
const char file_icon[] = "text-x-preview";

static GMainLoop *loop;

static void refuse_callback(NotifyNotification *n, const char *action,
			    gpointer user_data)
{
	GError *err = NULL;
	if (!notify_notification_close(n, &err))
		fprintf(stderr, "failed to close notification\n");

	if (err != NULL)
		GERROR(err);

	int *res = user_data;
	*res = 1;
}

static void accept_callback(NotifyNotification *n, const char *action,
			    gpointer user_data)
{
	GError *err = NULL;
	if (!notify_notification_close(n, &err))
		fprintf(stderr, "failed to close notification\n");

	if (err != NULL)
		GERROR(err);

	int *res = user_data;
	*res = 0;
}

static void closed_callback(NotifyNotification *n, gpointer user_data)
{
	int *res = user_data;
	if (*res == -1)
		*res = 1;

	g_main_loop_quit(loop);
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
	notify_uninit();
}

int request_notification(const char username[],
			 const char addr_str[INET_ADDRSTRLEN],
			 entry_type entry_type, off_t file_size,
			 const char filename[])
{
	const char *icon = entry_type == et_dir ? directory_icon : file_icon;

	int res = -1;

	char body[256];
	size_info size = bytes_to_size(file_size);
	snprintf(body, 256,
		 "Client %s (%s) wants to send you %s `%s` of size %.2lf %s",
		 username, addr_str, get_entry_type_name(entry_type), filename,
		 size.size, unit(size));

	NotifyNotification *n =
		notify_notification_new("File Transfer Request", body, icon);
	notify_notification_set_timeout(n, NOTIFY_EXPIRES_NEVER);
	GVariant *hint = g_variant_new_boolean(TRUE);
	notify_notification_set_category(n, "transfer");
	notify_notification_set_hint(n, "resident", hint);

	notify_notification_add_action(n, "refuse", "Refuse",
				       NOTIFY_ACTION_CALLBACK(refuse_callback),
				       &res, NULL);
	notify_notification_add_action(n, "accept", "Accept",
				       NOTIFY_ACTION_CALLBACK(accept_callback),
				       &res, NULL);
	g_signal_connect(n, "closed", G_CALLBACK(closed_callback), &res);

	GError *err = NULL;
	if (!notify_notification_show(n, &err)) {
		GERROR(err);
		return -1;
	}

	g_main_loop_run(loop);
	g_variant_unref(hint);

	return res;
}

int transfer_complete_notification(entry_type entry_type, const char filename[],
				   unsigned file_count)
{
	char details[223];
	snprintf(details, 223, "Downloaded %u files in total", file_count);

	char body[256];
	snprintf(body, 256, "Download of %s `%s` completed %s",
		 get_entry_type_name(entry_type), filename,
		 file_count > 1 ? details : "");

	NotifyNotification *n = notify_notification_new(
		"File Transfer Complete", body, "folder-download");
	notify_notification_set_category(n, "transfer.complete");

	GError *err = NULL;
	if (!notify_notification_show(n, &err)) {
		GERROR(err);
		return -1;
	}

	return 0;
}

int transfer_error_notification(const char body[])
{
	NotifyNotification *n = notify_notification_new(
		"File Transfer Error", body, "folder-download");
	notify_notification_set_category(n, "transfer.error");

	GError *err = NULL;
	if (!notify_notification_show(n, &err)) {
		GERROR(err);
		return -1;
	}

	return 0;
}
