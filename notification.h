#include <netinet/in.h>

#include "message.h"

typedef enum request_response {
	rr_refuse,
	rr_accept,
	rr_error
} request_response;

int notifications_init(const char *name);
void notifications_deinit(void);
request_response request_notification(char username[],
				      char addr_str[INET_ADDRSTRLEN],
				      request_data_t *request_data);
