#include "files.h"
#include <argp.h>
#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <poll.h>
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
#include "message.h"
#include "progress_bar.h"

typedef struct {
	int parsed;
	char *const downloads_dir;
	in_port_t port;
} args;

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
			{
				DIR *dir = opendir(a->downloads_dir);
				if (!dir)
					ERR("opendir");
				if (closedir(dir) < 0)
					ERR("closedir");
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
		fprintf(stderr, "parsing error :(");
		exit(EXIT_FAILURE);
	}

	*port = a.port;
}

typedef struct client {
	int socket;
	char addr_str[INET_ADDRSTRLEN];
	hello_data_t *info;
	file_t *files_metadata;
	size_t metadata_size;
} client_t;

#define TIMEOUT 1000

int setup(uint16_t port)
{
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		ERR("socket");

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int t = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
		ERR("setsockopt");
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		ERR("bind");
	if (listen(sock, 10) < 0)
		ERR("listen");

	return sock;
}

#define TIMEOUT 1000

bool recv_hello(client_t *client)
{
	struct pollfd p = {
		.fd = client->socket,
		.events = POLLIN,
	};

	if (poll(&p, 1, TIMEOUT) == 0) {
		fprintf(stderr, "client did not send data within timeout\n");
		return false;
	}

	header_t header;
	if (exchange_data_with_socket(client->socket, op_read, &header,
				      sizeof(header_t), NULL) < 0)
		ERR("recv");

	assert(header.type == mt_hello);

	hello_data_t *hello = malloc(header.data_size);
	if (exchange_data_with_socket(client->socket, op_read, hello,
				      header.data_size, NULL) < 0)
		ERR("recv");

	client->info = hello;

	header_t ack = {
		.type = mt_ack,
		.data_size = 0,
	};
	if (exchange_data_with_socket(client->socket, op_write, &ack,
				      sizeof(header_t), NULL) < 0)
		ERR("send");

	return true;
}

bool accept_client(int sock, client_t *client)
{
	printf("Waiting for a new client\n");

	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if ((client->socket = accept(sock, (struct sockaddr *)&addr, &len)) < 0)
		ERR("accept");

	if (!inet_ntop(AF_INET, &(addr.sin_addr), client->addr_str,
		       INET_ADDRSTRLEN))
		ERR("inet_ntop");

	if (!recv_hello(client))
		return false;

	printf("Client %s from address %s has connected\n",
	       client->info->username, client->addr_str);

	return true;
}

bool confirm_transfer(client_t *client, char path[PATH_MAX])
{
	header_t header;
	if (exchange_data_with_socket(client->socket, op_read, &header,
				      sizeof(header_t), NULL) < 0)
		ERR("recv");

	assert(header.type == mt_req);

	request_data_t *request = malloc(header.data_size);
	if (exchange_data_with_socket(client->socket, op_read, request,
				      header.data_size, NULL) < 0)
		ERR("recv");

	size_info size = bytes_to_size(request->total_file_size);
	printf("Do you want to receive %s `%.255s` of size %.2lf %s"
	       " from user %s at host %s [Y/n] ",
	       get_file_type_name(request->file_type), request->filename,
	       size.size, unit(size), client->info->username, client->addr_str);

	char *line = NULL;
	size_t len;
	if (getline(&line, &len, stdin) < 0)
		ERR("getline");
	char c = line[0];
	const bool accept = c == 'y' || c == 'Y' || c == '\n';
	free(line);

	header_t res = {
		.type = accept ? mt_ack : mt_nack,
		.data_size = 0,
	};

	if (exchange_data_with_socket(client->socket, op_write, &res,
				      sizeof(header_t), NULL) < 0)
		ERR("send");

	return accept;
}

void recv_metadata(client_t *client)
{
	header_t header;
	if (exchange_data_with_socket(client->socket, op_read, &header,
				      sizeof(header_t), NULL) < 0)
		ERR("recv");

	assert(header.type == mt_meta);

	client->metadata_size = header.data_size;
	client->files_metadata = malloc(header.data_size);
	if (exchange_data_with_socket(client->socket, op_read,
				      client->files_metadata, header.data_size,
				      NULL) < 0)
		ERR("recv");

	header_t ack = { .type = mt_ack, .data_size = 0 };
	if (exchange_data_with_socket(client->socket, op_write, &ack,
				      sizeof(header_t), NULL) < 0)
		ERR("send");
}

void recv_data(client_t *client, char path[PATH_MAX])
{
	files_iter it;
	files_iter_special_init(&it, client->files_metadata,
				client->metadata_size);

	file_t *file;
	file_data_t file_data;
	chdir(path);

	char title[PATH_MAX];
	struct timespec ts = { 0, 1e8 };
	progress_bar_t bar;
	while ((file = files_iter_next(&it))) {
		if (file->type == ft_dir) {
			if (mkdir(file->path, file->permissions) < 0)
				ERR("mkdir");
			continue;
		}

		if (open_and_map_file(file, &file_data, fo_write) < 0)
			ERR("open and map");
		if (ftruncate(file_data.fd, file_data.size) < 0)
			ERR("ftruncate");

		snprintf(title, PATH_MAX, "Receiving %s", file->path);
		prog_bar_init(&bar, title, file_data.size, ts);

		if (exchange_data_with_socket(client->socket, op_read,
					      file_data.map, file_data.size,
					      &bar) < 0)
			ERR("recv");

		destroy_file_data(&file_data);
	}
}

void cleanup_client(client_t *client)
{
	if (close(client->socket) < 0)
		ERR("close");

	free(client->info);

	printf("Disconnected client %s\n", client->addr_str);
}

int main(int argc, char *argv[])
{
	char downloads_directory[PATH_MAX];
	uint16_t port;

	read_args(argc, argv, &port, downloads_directory);

	int sock = setup(port);

	while (true) {
		client_t client = { 0 };
		char path[PATH_MAX];

		accept_client(sock, &client);
		if (!confirm_transfer(&client, path)) {
			cleanup_client(&client);
			continue;
		}

		recv_metadata(&client);
		recv_data(&client, downloads_directory);

		cleanup_client(&client);
	}

	close(sock);

	return EXIT_SUCCESS;
}
