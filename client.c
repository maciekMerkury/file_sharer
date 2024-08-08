#include "core.h"
#include "files.h"
#include "message.h"
#include "progress_bar.h"
#include "size_info.h"
#include <argp.h>
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
#include <time.h>
#include <unistd.h>

#define CLEANUP(label)              \
	do {                        \
		ret = EXIT_FAILURE; \
		goto label;         \
	} while (0)

typedef struct args {
	int parsed;
	in_port_t port;
	struct in_addr addr;
	char *path;
} args;

static inline int parse_path(args *restrict a, const char *path)
{
	a->path = malloc(PATH_MAX);
	if (!a->path)
		return -1;
	strcpy(a->path, path);
	return 0;
}

static inline int parse_addr(args *restrict a, const char *arg)
{
	if (inet_pton(AF_INET, arg, &a->addr) < 0) {
		fprintf(stderr, "ip: %s\t", arg);
		perror("");
		return -1;
	}

	return 0;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	args *a = state->input;

	switch (key) {
	case 'p':
		a->port = htons(atoi(arg));
		break;
	case ARGP_KEY_ARG:
		switch (a->parsed++) {
		case 0:
			if (parse_addr(a, arg) < 0)
				exit(EXIT_FAILURE);
			break;
		case 1:
			if (parse_path(a, arg) < 0)
				exit(EXIT_FAILURE);
			break;
		default:
			argp_usage(state);
		}
		break;
	case ARGP_KEY_END:
		if (a->parsed < 2)
			argp_usage(state);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

/* also performs the handshake, etc */
static int server_connect(int *dst_soc, struct in_addr addr, in_port_t port)
{
	int soc, ret = 0;

	puts("soc");
	if ((soc = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return -1;
	}

	struct sockaddr_in target = {
		.sin_addr = addr,
		.sin_family = AF_INET,
		.sin_port = port,
	};

	printf("port: %d\n", ntohs(port));
	if ((ret = connect(soc, (const struct sockaddr *)&target,
			   sizeof(struct sockaddr_in))) < 0) {
		perror("connect");
		goto soc_cleanup;
	}

	header_t header;
	hello_data_t *data;
	if (!(data = create_hello_message(&header))) {
		ret = -1;
		goto soc_cleanup;
	}

	puts("send_msg");
	if (send_msg(soc, &header, data) < 0) {
		ret = -1;
		goto hello_cleanup;
	}

	puts("get header");
	if (exchange_data_with_socket(soc, op_read, &header, sizeof(header_t),
				      NULL) < 0) {
		ret = -1;
		goto hello_cleanup;
	}

	puts("switch");
	switch (header.type) {
	case mt_ack:
		ret = 0;
		goto hello_cleanup;
	case mt_nack:
		fprintf(stderr, "server did not permit connection\n");
		ret = 1;
		goto hello_cleanup;
	default:
		fprintf(stderr, "invalid response from the server: %u\n",
			header.type);
		ret = -1;
		goto hello_cleanup;
	}

hello_cleanup:
	free(data);

soc_cleanup:
	if (ret < 0) {
		close(soc);
	} else {
		*dst_soc = soc;
	}

	return ret;
}

#define GOTO(label)                                             \
	do {                                                    \
		fprintf(stderr, "%s:%i\n", __FILE__, __LINE__); \
		goto label;                                     \
	} while (0)
#define read_header() \
	exchange_data_with_socket(soc, op_read, &h, sizeof(header_t), NULL)

/*
 * returns:
 *      -1 on failure
 *      0 on server accepting
 *      1 on server rejecting
 */
static int send_metadata(int soc, const files_t *metadata)
{
	header_t h;

	request_data_t *data = create_request_message(metadata, &h);
	if (!data)
		return -1;

	int ret = 0;

	if ((ret = send_msg(soc, &h, data)) < 0)
		GOTO(data_cleanup);

	puts("waiting for req ack");
	if ((ret = read_header()) < 0)
		GOTO(data_cleanup);
	/*
	if ((ret = exchange_data_with_socket(soc, op_read, &h, sizeof(header_t), NULL)) < 0)
		GOTO(data_cleanup);
	*/

	puts("req switch");
	switch (h.type) {
	case mt_ack:
		ret = 0;
		break;
	case mt_nack:
		ret = 1;
		GOTO(data_cleanup);
	default:
		ret = -1;
		GOTO(data_cleanup);
	}

	create_metadata_header(&h, metadata);

	if ((ret = send_msg(soc, &h, metadata->files)) < 0)
		GOTO(data_cleanup);

	puts("waiting for metadata ack");
	if ((ret = read_header()) < 0)
		GOTO(data_cleanup);

	puts("meta switch");
	switch (h.type) {
	case mt_ack:
		ret = 0;
		break;
	case mt_nack:
		ret = 1;
		GOTO(data_cleanup);
	default:
		ret = -1;
		GOTO(data_cleanup);
	}

data_cleanup:
	free(data);

	return ret;
}

static int send_all_files(files_t *fs, int soc)
{
	if (chdir(fs->parent_dir) < 0) {
		perror("chdir");
		return -1;
	}

	files_iter it;
	files_iter_init(&it, fs);
	file_t *ne;
	file_data_t fdata;

	progress_bar_t p;

	while ((ne = files_iter_next(&it))) {
		if (ne->type == ft_dir)
			continue;

		if (open_and_map_file(ne, &fdata, fo_read) < 0) {
			return -1;
		}

		prog_bar_init(&p, ne->path, ne->size,
			      (struct timespec){ .tv_nsec = 500e3 });

		const int ret = exchange_data_with_socket(
			soc, op_write, fdata.map, fdata.size, &p);
		destroy_file_data(&fdata);
		if (ret < 0)
			return -1;
	}

	return 0;
}

/* will do all the cleanup necessary */
static int client_main(in_port_t port, struct in_addr addr, char *file_path)
{
	puts("fs");
	files_t fs;
	if (create_files(file_path, &fs) < 0) {
		fprintf(stderr, "could not open file\n");
		exit(EXIT_FAILURE);
	}

	printf("%s\n", fs.root_dir_base);
	printf("%s\n", fs.parent_dir);

	puts("server");
	int ret = EXIT_SUCCESS;
	int server;
	if (server_connect(&server, addr, port) != 0) {
		CLEANUP(fs_cleanup);
	}

	switch (send_metadata(server, &fs)) {
	case 0:
		break;
	case 1:
		printf("server did not accept the transfer. exiting\n");
		CLEANUP(server_cleanup);
	case -1:
		perror("sending metadata");
		CLEANUP(server_cleanup);
	default:
		__builtin_unreachable();
	}

	size_info size = bytes_to_size(fs.total_file_size);
	printf("sending %s, size %.2lf%s\n", fs.root_dir_base, size.size,
	       unit(size));

	if ((ret = send_all_files(&fs, server)) < 0) {
		fprintf(stderr, "could not send all files\n");
		CLEANUP(server_cleanup);
	}

	ret = EXIT_SUCCESS;
server_cleanup:
	shutdown(server, SHUT_RDWR);
	close(server);

fs_cleanup:
	destroy_files(&fs);

	return ret;
}

int main(int argc, char **argv)
{
	const char *const args_doc = "IPv4 PATH";
	const struct argp_option options[] = {
		{ "port", 'p', "PORT", 0,
		  "change the server port from default (" STRINGIFY(
			  DEFAULT_PORT) ")" },
		{ 0 }
	};

	const struct argp arg_parser = {
		.options = options,
		.args_doc = args_doc,
		.parser = parse_opt,
	};

	args a = {
		.port = htons(DEFAULT_PORT),
	};

	if (argp_parse(&arg_parser, argc, argv, 0, NULL, &a) < 0) {
		return EXIT_FAILURE;
	}

	printf("addr: %s, path: %s, port: %u\n", inet_ntoa(a.addr), a.path,
	       a.port);

	return client_main(a.port, a.addr, a.path);
}
