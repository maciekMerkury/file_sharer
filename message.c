#define _GNU_SOURCE

#include "message.h"
#include <string.h>
#include <unistd.h>

void get_name(hello_data_t *hello)
{
	if (getlogin_r(hello->username, hello->username_len) < 0)
		strcpy(hello->username, default_user_name);
	hello->username_len = strlen(hello->username) + 1;
}
