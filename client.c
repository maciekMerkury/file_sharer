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
	int file_fd;
	const char *path;
} args;

typedef struct file {
	int fd;
	void *map;
	entry_t data;
} file;

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

	if ((a->file_fd = open(argv[2], O_RDONLY)) < 0) {
		perror("file open");
		return -1;
	}
	a->path = argv[2];

	if (argc == 4)
		a->port = htons(atoi(argv[3]));
	else
		a->port = htons(DEFAULT_PORT);

	return 0;
}

static int read_file(file *f, int fd, const char *const path)
{
#pragma message "actually support dirs"

	f->fd = fd;
	if (read_file_data(&f->data, path) < 0)
		return -1;

	if ((f->map = mmap(NULL, f->data.size, PROT_READ, MAP_FILE | MAP_SHARED,
			   fd, 0)) == MAP_FAILED) {
		perror("mmap");
		return -1;
	}

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

	if ((ret = send_all(&type, sizeof(message_type), soc, NULL)) < 0)
		goto hello_cleanup;

	if ((ret = send_all(hello, sizeof(hello_data_t) + hello->username_len,
			    soc, NULL)) < 0)
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

	if ((ret = send_all(&len, sizeof(size_t), soc, NULL)) < 0)
		goto data_cleanup;

	if ((ret = send_all(dat, len, soc, NULL)) < 0)
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
static int client_main(in_port_t port, struct in_addr addr, int file_fd,
		       const char *const file_path)
{
	int ret = EXIT_SUCCESS;
	file f;
	if (read_file(&f, file_fd, file_path) < 0)
		return EXIT_FAILURE;

	int server;
	if (server_connect(&server, addr, port) != 0)
		CLEANUP(file_cleanup);

	switch (send_metadata(server, &f.data)) {
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

	size_info size = bytes_to_size(f.data.size);
	printf("sending %s, size %.2lf%s\n", f.data.name, size.size,
	       unit(size));

	progress_bar_t bar;
	prog_bar_init(&bar, f.data.name, f.data.size,
		      (struct timespec){ .tv_nsec = 500e6 });

	ssize_t l = send_all(f.map, f.data.size, server, &bar);
	assert(l == f.data.size);
	if (l < 0) {
		perror("sending file");
		CLEANUP(server_cleanup);
	}

server_cleanup:
	shutdown(server, SHUT_RDWR);
	close(server);
file_cleanup:
	munmap(f.map, f.data.size);
	close(f.fd);
	entry_dealocate(&f.data);

	return ret;
}

int main(int argc, char **argv)
{
	args a;
	if (read_args(&a, argc, argv) < 0)
		return EXIT_FAILURE;

	return client_main(a.port, a.addr, a.file_fd, a.path);
}
