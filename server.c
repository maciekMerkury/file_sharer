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
#include "log.h"
#include "message.h"
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
			LOG_THROW(LOG_PERROR(!realpath(arg, a->downloads_dir),
					     "realpath"));
			LOG_THROW(LOG_ERRORF(
				!check_directory_exists(a->downloads_dir), -1,
				"Directory `%s` does not exist\n",
				a->downloads_dir));
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

	LOG_THROW(LOG_CALL(argp_parse(&argp, argc, argv, 0, NULL, &a) < 0));
	*port = a.port;
}

typedef struct client {
	char *download_dir;
	int socket;
	char addr_str[INET_ADDRSTRLEN];
	peer_info_t *info;
	stream_t entries;
} client_t;

#define TIMEOUT 1000
#define BACKLOG_SIZE 10

int setup(uint16_t port)
{
	int sock;
	LOG_THROW((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int t = 1;
	LOG_THROW(LOG_PERROR(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &t,
					sizeof(t)),
			     "setsockopt"));
	LOG_THROW(LOG_PERROR(bind(sock, (struct sockaddr *)&addr,
				  sizeof(addr)) < 0,
			     "bind"));
	LOG_THROW(LOG_PERROR(listen(sock, BACKLOG_SIZE) < 0, "listen"));

	return sock;
}

int recv_info(client_t *client)
{
	struct pollfd p = {
		.fd = client->socket,
		.events = POLLIN,
	};

	if (LOG_ERRORF(poll(&p, 1, TIMEOUT) == 0, 1,
		       "Client from host %s did not send data within timeout\n",
		       client->addr_str))
		return 1;

	header_t header;
	peer_info_t *info = NULL;

	if (LOG_CALL(perf_soc_op(client->socket, op_read, &header,
				 sizeof(header_t), NULL) < 0))
		return -1;

	if (LOG_ERRORF(header.type != mt_pinfo, 2,
		       "Client from host %s didn't send a peer info message\n",
		       client->addr_str))
		return 1;

	LOG_THROW(LOG_PERROR((info = malloc(header.data_size)) == NULL,
			     "malloc"));

	if (LOG_CALL(perf_soc_op(client->socket, op_read, info,
				 header.data_size, NULL) < 0))
		goto error;

	client->info = info;

	header_t ack = {
		.type = mt_ack,
		.data_size = 0,
	};
	if (LOG_CALL(perf_soc_op(client->socket, op_write, &ack,
				 sizeof(header_t), NULL) < 0))
		goto error;

	printf("Client %s from address %s has connected\n",
	       client->info->username, client->addr_str);

	return 0;

error:
	free(info);
	return -1;
}

int accept_client(int soc, client_t *client)
{
	printf("Waiting for a new client\n");

	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	client->socket = accept(soc, (struct sockaddr *)&addr, &len);
	if (LOG_PERROR((client->socket < 0), "accept"))
		return -1;

	if (LOG_PERROR(inet_ntop(AF_INET, &(addr.sin_addr), client->addr_str,
				 INET_ADDRSTRLEN) == NULL,
		       "inet_ntop"))
		return -1;

	return 0;
}

int confirm_transfer(client_t *client, char path[PATH_MAX])
{
	header_t header;
	if (LOG_CALL(perf_soc_op(client->socket, op_read, &header,
				 sizeof(header_t), NULL) < 0))
		return -1;

	if (LOG_ERRORF(header.type != mt_req, 1,
		       "Client %s from host %s didn't send a request message\n",
		       client->info->username, client->addr_str))
		return 1;

	request_data_t *request = malloc(header.data_size);
	if (LOG_PERROR(request == NULL, "malloc"))
		goto error;

	if (LOG_CALL(perf_soc_op(client->socket, op_read, request,
				 header.data_size, NULL) < 0))
		goto error;

	size_info size = bytes_to_size(request->total_file_size);
	printf("Do you want to receive %s `%.255s` of size %.2lf %s"
	       " from user %s at host %s [Y/n] ",
	       get_entry_type_name(request->entry_type), request->filename,
	       size.size, unit(size), client->info->username, client->addr_str);

	char *line = NULL;
	size_t len;
	if (LOG_PERROR((getline(&line, &len, stdin) < 0), "getline"))
		goto error;

	char c = line[0];
	const bool accept = c == 'y' || c == 'Y' || c == '\n';
	free(line);

	header_t res = {
		.type = accept ? mt_ack : mt_nack,
		.data_size = 0,
	};

	if (LOG_CALL(perf_soc_op(client->socket, op_write, &res,
				 sizeof(header_t), NULL) < 0))
		return -1;

	free(request);

	return accept ? 0 : 1;

error:
	free(request);

	return -1;
}

int recv_metadata(client_t *client)
{
	if (LOG_CALL(recv_stream(client->socket, &client->entries) < 0))
		return -1;

	header_t ack = { .type = mt_ack, .data_size = 0 };
	if (LOG_CALL(perf_soc_op(client->socket, op_write, &ack,
				 sizeof(header_t), NULL) < 0))
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

	bool error = false;

	const char *title_format = "Receiving %s";
	char title[PATH_MAX + 10];
	struct timespec ts = { 0, 1e8 };
	progress_bar_t bar;
	while ((entry = stream_iter_next(&it))) {
		if (entry->type == et_dir) {
			int ret = mkdir(entry->rel_path, entry->permissions);
			if (LOG_IGNORE(LOG_PERROR(ret < 0, "mkdir")))
				error = true;
			continue;
		}

		bool dealloc = false;
		if (LOG_IGNORE(LOG_CALL(get_entry_handles(entry, &entry_handles,
							  op_write) < 0))) {
			// we have to pretend to receive the data just becaue
			// we have no way of telling the client to not send it
			entry_handles.size = entry->size;
			entry_handles.map = malloc(entry->size);
			dealloc = true;

			LOG_THROW(LOG_PERROR(entry_handles.map == NULL,
					     "malloc"));

			error = true;
		} else {
			int r = ftruncate(entry_handles.fd, entry_handles.size);
			if (LOG_IGNORE(LOG_PERROR(r < 0, "ftruncate")))
				goto error;
		}

		snprintf(title, sizeof(title), title_format, entry->rel_path);
		prog_bar_init(&bar, title, entry_handles.size, ts);

		if (LOG_IGNORE((perf_soc_op(client->socket, op_read,
					    entry_handles.map,
					    entry_handles.size, &bar) < 0)))
			goto error;

error:
		if (dealloc)
			free(entry_handles.map);
		else
			close_entry_handles(&entry_handles);
	}

	if (error)
		fprintf(stderr, "Errors occurred, check the log for more info\n");
}

void cleanup_client(client_t *client)
{
	close(client->socket);

	printf("Disconnected client %s from host %s\n", client->info->username,
	       client->addr_str);

	free(client->info);
	destroy_stream(&client->entries);
}

void *handle_client(void *arg)
{
	logger_add_thread();
	client_t *client = arg;

	if (LOG_CALL(recv_info(client)))
		goto error;

	char path[PATH_MAX];

	if (LOG_CALL(confirm_transfer(client, path)))
		goto error;

	if (LOG_CALL(recv_metadata(client) < 0))
		goto error;

	LOG_CALLV(recv_data(client, client->download_dir));

	logger_remove_thread(1);

	return NULL;

error:
	cleanup_client(client);
	free(client);
	log_throw(__LINE__, __FILE__, __func__);

	return NULL;
}

int main(int argc, char *argv[])
{
	logger_init(NULL);
	logger_add_thread();

	char downloads_directory[PATH_MAX];
	uint16_t port;

	read_args(argc, argv, &port, downloads_directory);

	int soc;
	LOG_CALL(soc = setup(port));

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	while (true) {
		client_t *client = malloc(sizeof(client_t));
		if (LOG_IGNORE(LOG_PERROR(client == NULL, "malloc")))
			continue;

		*client = (client_t){ .download_dir = downloads_directory };
		if (LOG_IGNORE(LOG_CALL(accept_client(soc, client) < 0)))
			continue;

		pthread_t tid;
		LOG_IGNORE(LOG_PERROR(pthread_create(&tid, &attr, handle_client,
						     client),
				      "pthread_create"));
	}

	close(soc);

	logger_remove_thread(1);
	logger_deinit();

	return EXIT_SUCCESS;
}
