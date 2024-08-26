#include <libnotify/notify.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "notification.h"

#define GERROR(error)                                                      \
	(fprintf(stderr,                                                   \
		 "%s:%d: %s: gerror encountered\n"                         \
		 "domain: %u, code: %d\nmessage:%s\n",                     \
		 __FILE__, __LINE__, __func__, error->domain, error->code, \
		 error->message),                                          \
	 g_error_free(error))

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

int request_notification(const char body[], const char content_type[])
{
	int res = -1;

	const char *icon = g_content_type_get_generic_icon_name(content_type);

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

int transfer_complete_notification(const char body[])
{
	NotifyNotification *n = notify_notification_new(
		"File Transfer Complete", body, "network-receive");
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
	NotifyNotification *n = notify_notification_new("File Transfer Error",
							body, "network-error");
	notify_notification_set_category(n, "transfer.error");

	GError *err = NULL;
	if (!notify_notification_show(n, &err)) {
		GERROR(err);
		return -1;
	}

	return 0;
}
