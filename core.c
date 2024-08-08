#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core.h"
#include "progress_bar.h"

#define SOCKET_OPERATION(op, ret, ...)           \
	do {                                     \
		if (op == op_read) {             \
			ret = recv(__VA_ARGS__); \
		} else {                         \
			ret = send(__VA_ARGS__); \
		}                                \
	} while (0);

ssize_t exchange_data_with_socket(int soc, operation op, void *restrict buf,
				  size_t len,
				  progress_bar_t *const restrict prog_bar)
{
	int event = op_read ? POLLIN : POLLOUT;

	ssize_t sent = 0;
	int old_flags = 0;
	struct pollfd p = {
		.fd = soc,
		.events = event,
	};

	if (prog_bar) {
		old_flags = fcntl(soc, F_GETFL, 0);
		if (fcntl(soc, F_SETFL, old_flags | O_NONBLOCK) < 0) {
			perror("fcntl");
			return -1;
		}

		assert(!(old_flags & O_NONBLOCK));
		prog_bar_start(prog_bar);
	}

	ssize_t s;
	while (sent < len) {
		SOCKET_OPERATION(op, s, soc, (void *)((uintptr_t)buf + sent),
				 len - sent, 0);
		if (s < 0) {
			if (errno != EWOULDBLOCK) {
				perror("send");
				sent = s;
				break;
			}
		} else {
			sent += s;
		}

		if (prog_bar) {
			prog_bar_advance(prog_bar, sent);
			int ret = poll(&p, 1, DEFAULT_POLL_TIMEOUT);
			if (ret == 0) {
				fprintf(stderr, "sending timed out\n");
				sent = -1;
				break;
			} else if (ret < 0) {
				perror("send_all poll");
				sent = -1;
				break;
			}
			assert(ret == 1);
		}
	}

	if (prog_bar) {
		if (fcntl(soc, F_SETFL, old_flags) < 0) {
			perror("fcntl");
			return -1;
		}
		prog_bar_finish(prog_bar);
	}

	return sent;
}

void const *memcpyy(void *restrict dest, const void *restrict src, size_t len)
{
	memcpy(dest, src, len);
	return (void *)((uintptr_t)src + len);
}

int send_msg(int soc, header_t *h, void *data)
{
	if (exchange_data_with_socket(soc, op_write, h, sizeof(header_t),
				      NULL) < 0)
		return -1;

	if (exchange_data_with_socket(soc, op_write, data, h->data_size, NULL) <
	    0)
		return -1;

	return 0;
}

int receive_msg(int soc, header_t *restrict h, void *restrict *data)
{
	if (exchange_data_with_socket(soc, op_read, h, sizeof(header_t), NULL) <
	    0)
		return -1;

	*data = realloc(*data, h->data_size);

	if (!*data) {
		perror("realloc");
		return -1;
	}

	if (exchange_data_with_socket(soc, op_read, *data, h->data_size, NULL) <
	    0) {
		free(*data);
		return -1;
	}

	return 0;
}

#define UNIT_LEN 4
static const char size_units[UNIT_LEN][4] = { "B", "KiB", "MiB", "GiB" };

size_info bytes_to_size(size_t byte_size)
{
	double size = byte_size;
	unsigned int idx = 0;

	while (size >= 1024.0) {
		idx++;
		size /= 1024.0;
	}

	return (size_info){
		.size = size,
		.unit_idx = idx,
	};
}

const char *const unit(size_info info)
{
	return (info.unit_idx < UNIT_LEN) ? size_units[info.unit_idx] : NULL;
}
