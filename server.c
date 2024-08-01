#include "progress_bar.h"
#include <errno.h>
#include <poll.h>
#include <linux/limits.h>
#include <poll.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <pwd.h>
#include <sys/socket.h>

#include "message.h"
#include "size_info.h"
#include "core.h"

typedef struct client {
	int socket;
	char addr_str[INET_ADDRSTRLEN];
	size_t name_len;
	char *name;
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

void read_name(client_t *client)
{
	if (recv(client->socket, &client->name_len, sizeof(size_t), 0) < 0)
		ERR("recv");

	printf("%lu\n", client->name_len);
	client->name = malloc(client->name_len);
	if (client->name == NULL)
		ERR("malloc");

	if (recv(client->socket, client->name, client->name_len, 0) < 0)
		ERR("recv");
}

void accept_client(int sock, client_t *client)
{
	printf("Waiting for a new client\n");

	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if ((client->socket = accept(sock, (struct sockaddr *)&addr, &len)) < 0)
		ERR("accept");

	if (!inet_ntop(AF_INET, &(addr.sin_addr), client->addr_str,
		       INET_ADDRSTRLEN))
		ERR("inet_ntop");

	message_type t;
	struct pollfd p = {
		.fd = client->socket,
		.events = POLLIN,
	};

	if (poll(&p, 1, TIMEOUT) == 0) {
		fprintf(stderr, "client did not send data within timeout\n");
		exit(EXIT_FAILURE);
	}

	if (recv(client->socket, &t, sizeof(message_type), 0) < 0)
		ERR("recv");

	if (t != mt_hello) {
		fprintf(stderr, "handshake error: %u\n", t);
		exit(EXIT_FAILURE);
	}

	read_name(client);

	printf("client %s from address %s has connected\n", client->name,
	       client->addr_str);
}

bool confirm_transfer(client_t *client, file_data_t *data, char path[PATH_MAX])
{
	if (recv(client->socket, data, sizeof(file_data_t), 0) < 0)
		ERR("recv");

	size_info size = bytes_to_size(data->size);

	printf("Host %s wants to send you file `%.255s` of size %.2lf %s\n",
	       client->addr_str, data->name, size.size, unit(size));

    char *line = NULL;
    size_t len;
    if (getline(&line, &len, stdin) < 0)
        ERR("getline");
    char c = line[0];

    printf("%s\n", line);
	return c == 'y' || c == 'Y' || c == '\n';
}

void receive_file(client_t *client, file_data_t *data, char path[PATH_MAX])
{
	int fd;
	if ((fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_EXCL, 0644)) < 0)
		ERR("open");
	if (ftruncate(fd, data->size) < 0)
		ERR("ftruncate");

	void *file;
	if ((file = mmap(NULL, data->size, PROT_WRITE, MAP_FILE | MAP_SHARED,
			 fd, 0)) == MAP_FAILED)
		ERR("mmap");

	size_t total_received = 0;
	ssize_t recv_size;

	int flags;
	if ((flags = fcntl(client->socket, F_GETFL)) < 0)
		ERR("fcntl");
	if (fcntl(client->socket, F_SETFL, flags | O_NONBLOCK) < 0)
		ERR("fcntl");

	char title[PATH_MAX];
	snprintf(title, PATH_MAX, "Receiving %s", path);

	progress_bar_t bar;
	prog_bar_init(&bar, title, data->size,
		      (struct timespec){ 0, 100000000 });

	prog_bar_start(&bar);
	while (total_received < data->size) {
		if ((recv_size =
			     recv(client->socket,
				  (void *)((uintptr_t)file + total_received),
				  (data->size - total_received), 0)) < 0) {
			if (errno != EWOULDBLOCK)
				ERR("recv");
		} else {
			total_received += recv_size;
			prog_bar_advance(&bar, total_received);
		}
	}
	prog_bar_finish(&bar);

	printf("\nDone receiving file\n");

	if (fcntl(client->socket, F_SETFL, flags) < 0)
		ERR("fcntl");

	if (munmap(file, data->size) < 0)
		ERR("munmap");
	if (close(fd) < 0)
		ERR("close");
}

void cleanup_client(client_t *client)
{
	if (close(client->socket) < 0)
		ERR("close");

	printf("Disconnected client %s\n", client->addr_str);
}

void usage(char *prog)
{
	fprintf(stderr, "USAGE: %s port download_directory\n", prog);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	if (argc < 3)
		usage(argv[0]);

	uint16_t port = atoi(argv[1]);

	int sock = setup(port);

	// check if it's a valid directory
	char downloads_directory[PATH_MAX];
	realpath(argv[2], downloads_directory);

	while (true) {
		client_t client = { 0 };
		file_data_t data;
		char path[PATH_MAX];

		accept_client(sock, &client);
		if (!confirm_transfer(&client, &data, path)) {
			cleanup_client(&client);
			continue;
		}

		strcpy(path, downloads_directory);
		strcat(path, "/");
		strcat(path, data.name);
		receive_file(&client, &data, path);
		cleanup_client(&client);
	}

	close(sock);

	return EXIT_SUCCESS;
}
