#include <netinet/in.h>

int notifications_init(const char *name);
void notifications_deinit(void);

int request_notification(const char body[], const char content_type[]);
int transfer_complete_notification(const char body[]);
int transfer_error_notification(const char body[]);
