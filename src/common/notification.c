#include <libnotify/notify.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "notification.h"

#define LOG_GERROR(error, msg)                                                \
	error ? (LOG_ERRORF(true, error->code,                                \
			    "%s\n %s:%d: %s: gerror encountered\n"            \
			    "domain: %u, code: %d\nmessage:%s\n",             \
			    msg, __FILE__, __LINE__, __func__, error->domain, \
			    error->code, error->message),                     \
		 g_error_free(error), 1) :                                    \
		0

static GMainLoop *loop;
static NotifyNotification *progress_notification;

static void refuse_callback(NotifyNotification *n, const char *action,
			    gpointer user_data)
{
	GError *err = NULL;
	notify_notification_close(n, &err);
	LOG_IGNORE(LOG_GERROR(err, "failed to close notification"));

	int *res = user_data;
	*res = 1;
}

static void accept_callback(NotifyNotification *n, const char *action,
			    gpointer user_data)
{
	GError *err = NULL;
	notify_notification_close(n, &err);
	LOG_IGNORE(LOG_GERROR(err, "failed to close notification"));

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
	if (LOG_ERROR(!notify_init(name), 1, "failed to initalize libnotify"))
		return -1;

	loop = g_main_loop_new(NULL, FALSE);

	return 0;
}

void notifications_deinit(void)
{
	g_main_loop_unref(loop);
	loop = NULL;
	notify_uninit();
}

int request_notification(const char body[], const char content_type[])
{
	int res = -1;

	char *icon = g_content_type_get_generic_icon_name(content_type);

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
	notify_notification_show(n, &err);
	if (LOG_GERROR(err, "failed to show notification"))
		return -1;

	g_free(icon);
	g_main_loop_run(loop);
	g_variant_unref(hint);

	return res;
}

int transfer_in_progress_notification(const char body[], float perc)
{
	if (progress_notification == NULL) {
		progress_notification = notify_notification_new(
			"File Transfer In Progress", body, "network-receive");
		notify_notification_set_category(progress_notification,
						 "transfer");
		GVariant *hint = g_variant_new_boolean(TRUE);
		notify_notification_set_hint(progress_notification, "resident",
					     hint);
	} else
		notify_notification_update(progress_notification,
					   "File Transfer In Progress", body,
					   "network-receive");

	GVariant *hint = g_variant_new_int32((int)(perc * 100));
	notify_notification_set_hint(progress_notification, "value", hint);

	GError *err = NULL;
	notify_notification_show(progress_notification, &err);
	if (LOG_GERROR(err, "failed to show notification"))
		return -1;

	return 0;
}

int transfer_complete_notification(const char body[])
{
	NotifyNotification *n = progress_notification;
	if (n == NULL)
		n = notify_notification_new("File Transfer Complete", body,
					    "network-receive");
	else
		notify_notification_update(n, "File Transfer Complete", body,
					   "network-receive");

	notify_notification_set_category(n, "transfer.complete");

	GError *err = NULL;
	notify_notification_show(n, &err);
	if (LOG_GERROR(err, "failed to show notification"))
		return -1;
	n = NULL;

	return 0;
}

int transfer_error_notification(const char body[])
{
	NotifyNotification *n = notify_notification_new("File Transfer Error",
							body, "network-error");
	notify_notification_set_category(n, "transfer.error");

	GError *err = NULL;
	notify_notification_show(n, &err);
	if (LOG_GERROR(err, "failed to show notification"))
		return -1;

	return 0;
}
