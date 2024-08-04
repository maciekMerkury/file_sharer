#include "core.h"
#include "entry.h"
#include "message.h"
#include "progress_bar.h"
#include "size_info.h"
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define CLEANUP(label)              \
	do {                        \
		ret = EXIT_FAILURE; \
		goto label;         \
	} while (0)

#define DEFAULT_PORT 2137

typedef struct args {
	in_port_t port;
	struct in_addr addr;
	char *path;
} args;

static int read_args(args *a, int argc, char **argv)
{
	if (argc < 3) {
		printf("USAGE: %s IPv4 FILE_PATH [PORT]\n", argv[0]);
		return -1;
	}

	if (inet_pton(AF_INET, argv[1], &a->addr) < 0) {
		fprintf(stderr, "ip: %s\t", argv[1]);
		perror("");
		return -1;
	}

	a->path = malloc(PATH_MAX);
	const size_t len = strlen(argv[2]);
	memcpy(a->path, argv[2], len+sizeof(char));

	struct stat s;
	if (stat(a->path, &s) < 0) {
		perror("stat");
		return -1;
	}

	if (!S_ISDIR(s.st_mode) && !S_ISREG(s.st_mode)) {
		fprintf(stderr, "only regular files and directories are supported\n");
		return -1;
	}

	if (argc == 4)
		a->port = htons(atoi(argv[3]));
	else
		a->port = htons(DEFAULT_PORT);

	return 0;
}

/* also performs the handshake, etc */
static int server_connect(int *dst_soc, struct in_addr addr, in_port_t port)
{
	int soc, ret;

	if ((soc = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return -1;
	}

	struct sockaddr_in target = {
		.sin_addr = addr,
		.sin_family = AF_INET,
		.sin_port = port,
	};

	if ((ret = connect(soc, (const struct sockaddr *)&target,
			   sizeof(struct sockaddr_in))) < 0) {
		perror("connect");
		goto soc_cleanup;
	}

	hello_data_t *hello = malloc(MAX_HELLO_DATA_SIZE);
	hello->username_len = NAME_MAX + 1;

	get_name(hello);

	message_type type = mt_hello;

	if ((ret = exchange_data_with_socket(soc, op_write, &type,
					     sizeof(message_type), NULL)) < 0)
		goto hello_cleanup;

	if ((ret = exchange_data_with_socket(
		     soc, op_write, hello,
		     sizeof(hello_data_t) + hello->username_len, NULL)) < 0)
		goto hello_cleanup;

	if ((ret = recv(soc, &type, sizeof(message_type), 0)) < 0)
		goto hello_cleanup;

	switch (type) {
	case mt_ack:
		ret = 0;
		goto hello_cleanup;
	case mt_nack:
		fprintf(stderr, "server did not permit connection\n");
		ret = 1;
		goto hello_cleanup;
	default:
		fprintf(stderr, "invalid response from the server: %u\n", type);
		ret = -1;
		goto hello_cleanup;
	}

hello_cleanup:
	free(hello);
soc_cleanup:
	if (ret == 0)
		*dst_soc = soc;
	else
		close(soc);
	return ret;
}

/*
 * returns:
 *      -1 on failure
 *      0 on server accepting
 *      1 on server rejecting
 */
static int send_metadata(int soc, const entry_t *entry)
{
	int ret = 0;
	size_t len = total_entry_len(entry);
	void *dat = malloc(len);
	if (deflate_entry(entry, dat, len) != dat) {
		ret = -1;
		goto data_cleanup;
	}

	if ((ret = exchange_data_with_socket(soc, op_write, &len,
					     sizeof(size_t), NULL)) < 0)
		goto data_cleanup;

	if ((ret = exchange_data_with_socket(soc, op_write, dat, len, NULL)) <
	    0)
		goto data_cleanup;

	// this is ugly
data_cleanup:
	free(dat);
	if (ret < 0)
		return -1;

	message_type t;
	if (recv(soc, &t, sizeof(t), 0) < 0)
		return -1;

	switch (t) {
	case mt_ack:
		return 0;
	case mt_nack:
		return 1;
	default:
		return -1;
	}
}

/* will do all the cleanup necessary */
static int client_main(in_port_t port, struct in_addr addr, char *file_path)
{
	int ret = EXIT_SUCCESS;
	entry_t base;
	if (read_entry(&base, file_path) < 0)
		return EXIT_FAILURE;

	int server;
	if (server_connect(&server, addr, port) != 0)
		CLEANUP(file_cleanup);

	switch (send_metadata(server, &base)) {
	case 0:
		printf("server accepted\n");
		break;
	case 1:
		printf("server did not accept the transfer. exiting\n");
		CLEANUP(server_cleanup);
		break;
	case -1:
		perror("sending metadata");
		CLEANUP(server_cleanup);
		break;
	default:
		fprintf(stderr, "sus\n");
		exit(21);
	}

	size_info size = bytes_to_size(base.size);
	printf("sending %s, size %.2lf%s\n", base.name, size.size,
	       unit(size));

	progress_bar_t bar;
	prog_bar_init(&bar, base.name, base.size,
		      (struct timespec){ .tv_nsec = 500e6 });

	ssize_t l = exchange_data_with_socket(server, op_write, f.map,
					      f.data.size, &bar);
	assert(l == f.data.size);
	if (l < 0) {
		perror("sending file");
		CLEANUP(server_cleanup);
	}

server_cleanup:
	shutdown(server, SHUT_RDWR);
	close(server);
file_cleanup:
	entry_deallocate(&base);

	free(file_path);

	return ret;
}

int main(int argc, char **argv)
{
	args a;
	if (read_args(&a, argc, argv) < 0)
		return EXIT_FAILURE;

	return client_main(a.port, a.addr, a.path);
}
