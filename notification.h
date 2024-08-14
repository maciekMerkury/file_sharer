#include <netinet/in.h>

#include "entry.h"

int notifications_init(const char *name);
void notifications_deinit(void);

int request_notification(const char username[],
			 const char addr_str[INET_ADDRSTRLEN],
			 entry_type entry_type, off_t file_size,
			 const char filename[]);
int transfer_complete_notification(entry_type entry_type, const char filename[],
				   unsigned file_count);
int transfer_error_notification(const char body[]);
