#include <argp.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core.h"
#include "entry.h"
#include "message.h"
#include "notification.h"
#include "progress_bar.h"

typedef struct {
	int parsed;
	char *const downloads_dir;
	in_port_t port;
} args;

bool check_directory_exists(char path[PATH_MAX])
{
	DIR *dir = opendir(path);
	if (!dir)
		return false;
	closedir(dir);

	return true;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	args *a = state->input;
	switch (key) {
	case ARGP_KEY_ARG:
		switch (a->parsed++) {
		case 0:
			a->port = atoi(arg);
			break;
		case 1:
			// this should probably handle errors
			realpath(arg, a->downloads_dir);
			if (!check_directory_exists(a->downloads_dir)) {
				fprintf(stderr, "Directory %s does not exist\n",
					a->downloads_dir);
				exit(EXIT_FAILURE);
			}
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
	};
	return 0;
}

void read_args(int argc, char *argv[], uint16_t *port,
	       char downloads_directory[PATH_MAX])
{
	const char *const args_doc = "PORT DOWNLOAD_PATH";
	const struct argp_option options[] = { 0 };
	const struct argp argp = {
		.options = options,
		.args_doc = args_doc,
		.parser = parse_opt,
	};
	args a = { .parsed = 0, .downloads_dir = downloads_directory };

	if (argp_parse(&argp, argc, argv, 0, NULL, &a) < 0) {
		fprintf(stderr, "parsing error :(\n");
		exit(EXIT_FAILURE);
	}

	*port = a.port;
}

typedef struct client {
	int socket;
	char addr_str[INET_ADDRSTRLEN];
	hello_data_t *info;
	stream_t entries;
} client_t;

#define TIMEOUT 1000
#define BACKLOG_SIZE 10

int setup(uint16_t port)
{
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		ERR_EXIT("socket");

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int t = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
		ERR_EXIT("setsockopt");
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		ERR_EXIT("bind");
	if (listen(sock, BACKLOG_SIZE) < 0)
		ERR_EXIT("listen");

	return sock;
}

int recv_hello(client_t *client)
{
	struct pollfd p = {
		.fd = client->socket,
		.events = POLLIN,
	};

	if (poll(&p, 1, TIMEOUT) == 0) {
		fprintf(stderr,
			"Client from host %s did not send data within timeout\n",
			client->addr_str);
		return 1;
	}

	header_t header;
	hello_data_t *hello = NULL;

	if (perf_soc_op(client->socket, op_read, &header, sizeof(header_t),
			NULL) < 0)
		return -1;

	if (header.type != mt_hello) {
		fprintf(stderr,
			"Client from host %s didn't send a hello message\n",
			client->addr_str);
		return 1;
	}

	hello = malloc(header.data_size);
	if (hello == NULL)
		ERR_GOTO("malloc");

	if (perf_soc_op(client->socket, op_read, hello, header.data_size,
			NULL) < 0)
		goto error;

	client->info = hello;

	header_t ack = {
		.type = mt_ack,
		.data_size = 0,
	};
	if (perf_soc_op(client->socket, op_write, &ack, sizeof(header_t),
			NULL) < 0)
		goto error;

	return 0;

error:
	free(hello);
	return -1;
}

bool accept_client(int sock, client_t *client)
{
	printf("Waiting for a new client\n");

	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if ((client->socket = accept(sock, (struct sockaddr *)&addr, &len)) < 0)
		ERR_EXIT("accept");

	if (!inet_ntop(AF_INET, &(addr.sin_addr), client->addr_str,
		       INET_ADDRSTRLEN))
		ERR_EXIT("inet_ntop");

	if (recv_hello(client))
		return false;

	printf("Client %s from address %s has connected\n",
	       client->info->username, client->addr_str);

	return true;
}

int confirm_transfer(client_t *client, char path[PATH_MAX])
{
	header_t header;
	if (perf_soc_op(client->socket, op_read, &header, sizeof(header_t),
			NULL) < 0)
		return -1;

	if (header.type != mt_req) {
		fprintf(stderr,
			"Client %s from host %s didn't send a request messsage\n",
			client->info->username, client->addr_str);
		return 1;
	}

	request_data_t *request = malloc(header.data_size);
	if (perf_soc_op(client->socket, op_read, request, header.data_size,
			NULL) < 0)
		goto error;

	const bool accept = request_notification(client->info->username,
						 client->addr_str,
						 request) == rr_accept;

	header_t res = {
		.type = accept ? mt_ack : mt_nack,
		.data_size = 0,
	};

	if (perf_soc_op(client->socket, op_write, &res, sizeof(header_t),
			NULL) < 0)
		return -1;

	free(request);

	return accept ? 0 : 1;

error:
	free(request);

	return -1;
}

int recv_metadata(client_t *client)
{
	if (recv_stream(client->socket, &client->entries) < 0)
		return -1;

	header_t ack = { .type = mt_ack, .data_size = 0 };
	if (perf_soc_op(client->socket, op_write, &ack, sizeof(header_t),
			NULL) < 0)
		return -1;

	return 0;
}

void recv_data(client_t *client, char path[PATH_MAX])
{
	stream_iter_t it;
	stream_iter_init(&it, &client->entries);

	entry_t *entry;
	entry_handles_t entry_handles;
	chdir(path);

	const char *title_format = "Receiving %s";
	char title[PATH_MAX + 10];
	struct timespec ts = { 0, 1e8 };
	progress_bar_t bar;
	while ((entry = stream_iter_next(&it))) {
		if (entry->type == et_dir) {
			if (mkdir(entry->rel_path, entry->permissions) < 0)
				PERROR("mkdir");
			continue;
		}

		if (get_entry_handles(entry, &entry_handles, op_write) < 0)
			continue;
		if (ftruncate(entry_handles.fd, entry_handles.size) < 0)
			ERR_GOTO("ftruncate");

		snprintf(title, sizeof(title), title_format, entry->rel_path);
		prog_bar_init(&bar, title, entry_handles.size, ts);

		if (perf_soc_op(client->socket, op_read, entry_handles.map,
				entry_handles.size, &bar) < 0)
			goto error;

error:
		close_entry_handles(&entry_handles);
	}
}

void cleanup_client(client_t *client)
{
	close(client->socket);

	printf("Disconnected client %s from host %s\n", client->info->username,
	       client->addr_str);

	free(client->info);
	destroy_stream(&client->entries);
}

void handle_client(client_t *client, char downloads_directory[PATH_MAX])
{
	char path[PATH_MAX];

	if (confirm_transfer(client, path))
		goto cleanup;

	if (recv_metadata(client) < 0)
		goto cleanup;

	recv_data(client, downloads_directory);

cleanup:
	cleanup_client(client);
}

int main(int argc, char *argv[])
{
	notifications_init("file-sharer server");

	char downloads_directory[PATH_MAX];
	uint16_t port;

	read_args(argc, argv, &port, downloads_directory);

	int sock = setup(port);

	while (true) {
		client_t client = { 0 };

		if (accept_client(sock, &client))
			handle_client(&client, downloads_directory);
	}

	notifications_deinit();
	close(sock);

	return EXIT_SUCCESS;
}
