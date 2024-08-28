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

#include "log.h"
#include "core.h"
#include "entry.h"
#include "message.h"
#include "progress_bar.h"

typedef struct args {
	int parsed;
	in_port_t port;
	struct in_addr addr;
	char *path;
} args;

static inline int parse_path(args *restrict a, const char *path)
{
	a->path = malloc(PATH_MAX);
	if (LOG_PERROR(a->path == NULL, "malloc"))
		return -1;
	strcpy(a->path, path);
	return 0;
}

static inline int parse_addr(args *restrict a, const char *arg)
{
	if (LOG_PERROR(inet_pton(AF_INET, arg, &a->addr) < 0, "inet_pton"))
		return -1;

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
			LOG_THROW(LOG_CALL(parse_addr(a, arg) < 0));
			break;
		case 1:
			LOG_THROW(LOG_CALL(parse_path(a, arg) < 0));
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
	int soc;
	if (LOG_PERROR((soc = socket(AF_INET, SOCK_STREAM, 0)) < 0, "socket"))
		return -1;

	struct sockaddr_in target = {
		.sin_addr = addr,
		.sin_family = AF_INET,
		.sin_port = port,
	};

	header_t header;
	peer_info_t *data = NULL;

	printf("port: %d\n", ntohs(port));
	if (LOG_PERROR(connect(soc, (const struct sockaddr *)&target,
			       sizeof(struct sockaddr_in)) < 0,
		       "connect"))
		goto error;

	if (LOG_CALL((data = create_pinfo_message(&header)) == NULL))
		goto error;

	if (LOG_CALL(send_msg(soc, &header, data) < 0))
		goto error;

	if (LOG_CALL(perf_soc_op(soc, op_read, &header, sizeof(header_t),
				 NULL) < 0))
		goto error;

	switch (header.type) {
	case mt_ack:
		break;
	case mt_nack:
		fprintf(stderr, "server did not permit connection\n");
		goto refused;
	default:
		fprintf(stderr, "invalid response from the server: %u\n",
			header.type);
		goto error;
	}

	*dst_soc = soc;
	free(data);
	return 0;

error:
	free(data);
	close(soc);
	return -1;

refused:
	free(data);
	close(soc);
	return 1;
}

#define read_header() \
	LOG_CALL(perf_soc_op(soc, op_read, &h, sizeof(header_t), NULL))

static int send_metadata(int soc, entries_t *metadata)
{
	header_t h;

	request_data_t *data;
	if (LOG_CALL((data = create_request_message(metadata, &h)) == NULL))
		return -1;

	if (LOG_CALL(send_msg(soc, &h, data) < 0))
		goto error;

	if (read_header() < 0)
		goto error;

	switch (h.type) {
	case mt_ack:
		break;
	case mt_nack:
		goto refused;
	default:
		goto error;
	}

	if (LOG_CALL(send_stream(soc, &metadata->entries) < 0))
		goto error;

	if (read_header() < 0)
		goto error;

	switch (h.type) {
	case mt_ack:
		break;
	case mt_nack:
		goto refused;
	default:
		goto error;
	}

	free(data);
	return 0;

error:
	free(data);
	return -1;

refused:
	printf("server did not accept the transfer\n");
	free(data);
	return 1;
}

static int send_all_files(entries_t *fs, int soc)
{
	int fd;
	if (LOG_PERROR((fd = open(fs->parent_path, O_DIRECTORY)) < 0, "open"))
		return -1;

	stream_iter_t it;
	stream_iter_init(&it, &fs->entries);
	entry_t *ne;
	entry_handles_t fdata;

	prog_bar_t p;

	prog_bar_init(&p, &fs->entries, fs->total_file_size);
	while ((ne = stream_iter_next(&it))) {
		prog_bar_next(&p);
		if (ne->type == et_dir)
			continue;

		if (LOG_CALL(get_entry_handles(fd, ne, &fdata, op_read) < 0))
			return -1;

		if (LOG_CALL(perf_soc_op(soc, op_write, fdata.map, fdata.size,
					 &p) < 0))
			goto error;

		close_entry_handles(&fdata);
	}

	return 0;

error:
	close_entry_handles(&fdata);
	return -1;
}

/* will do all the cleanup necessary */
static int client_main(in_port_t port, struct in_addr addr, char *file_path)
{
	entries_t fs;
	LOG_THROW(LOG_CALL(create_entries(file_path, &fs) < 0));

	int server = -1;
	int ret = 0;
	if (LOG_CALL((ret = server_connect(&server, addr, port))) != 0)
		goto cleanup;

	if (LOG_CALL(send_metadata(server, &fs) != 0))
		goto cleanup;

	size_info size = bytes_to_size(fs.total_file_size);
	printf("sending %s, size %.2lf%s\n",
	       get_entry_rel_path((((entry_t *)fs.entries.data)->data)),
	       size.size, unit(size));

	if (LOG_CALL(send_all_files(&fs, server) < 0))
		goto cleanup;

cleanup:
	if (server >= 0) {
		shutdown(server, SHUT_RDWR);
		close(server);
	}

	destroy_entries(&fs);
	free(file_path);

	return ret;
}

int main(int argc, char **argv)
{
	logger_init(NULL);
	logger_add_thread();

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

	if (LOG_CALL(argp_parse(&arg_parser, argc, argv, 0, NULL, &a) < 0))
		exit(EXIT_FAILURE);

	printf("addr: %s, path: %s, port: %u\n", inet_ntoa(a.addr), a.path,
	       a.port);

	LOG_THROW(LOG_CALL(client_main(a.port, a.addr, a.path) < 0));

	logger_remove_thread(1);
	logger_deinit();

	return EXIT_SUCCESS;
}
