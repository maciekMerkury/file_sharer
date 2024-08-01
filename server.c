#include <errno.h>
#include <poll.h>
#include <linux/limits.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
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

#include "progress_bar.h"
#include "message.h"
#include "entry.h"
#include "size_info.h"
#include "core.h"

typedef struct client {
	int socket;
	char addr_str[INET_ADDRSTRLEN];
	size_t name_len;
	char *name;
} client_t;

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

	client->name = malloc(client->name_len);
	if (client->name == NULL)
		ERR("malloc");

	if (poll(&p, 1, TIMEOUT) == 0) {
		fprintf(stderr, "client did not send data within timeout\n");
		return false;
	}

	message_type mt;
	if (recv(client->socket, &mt, sizeof(message_type), 0) < 0)
		ERR("recv");

	if (mt != mt_hello) {
		fprintf(stderr, "handshake error: %u\n", mt);
		return false;
	}

	if (recv(client->socket, &client->name_len, sizeof(size_t), 0) < 0)
		ERR("recv");

	client->name = malloc(client->name_len);
	if (client->name == NULL)
		ERR("malloc");

	if (recv(client->socket, client->name, client->name_len, 0) < 0)
		ERR("recv");

	return true;
}

void recv_entry(client_t *client, entry_t *entry)
{
	message_type mt;
	if (recv(client->socket, &mt, sizeof(message_type), 0) < 0)
		ERR("recv");

	size_t msg_size = mt == mt_file ? sizeof(file_data_t) :
					  sizeof(dir_data_t);

	entry->type = mt == mt_file ? et_file : et_directory;

	if (recv(client->socket, &entry->data, msg_size, 0) < 0)
		ERR("recv");
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

	printf("client %s from address %s has connected\n", client->name,
	       client->addr_str);

	return true;
}

bool confirm_transfer(client_t *client, entry_t *entry, char path[PATH_MAX])
{
	recv_entry(client, entry);

	size_info size = bytes_to_size(get_entry_size(entry));
	printf("Do you want to receive %s `%.255s` of size %s"
	       " from user %s at host %s [Y/n] ",
           get_entry_type_name(entry), get_entry_name(entry),
	       unit(size), client->name, client->addr_str);
	       

	char *line = NULL;
	size_t len;
	if (getline(&line, &len, stdin) < 0)
		ERR("getline");
	char c = line[0];

	return c == 'y' || c == 'Y' || c == '\n';
}

void recv_entry_data(client_t *client, entry_t *entry, char path[PATH_MAX])
{
	message_type mt = mt_ack;
	if (send(client->socket, &mt, sizeof(message_type), 0) < 0)
		ERR("send");

	int fd;
	if ((fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_EXCL, 0644)) < 0)
		ERR("open");
	if (ftruncate(fd, get_entry_size(entry)) < 0)
		ERR("ftruncate");

	void *file;
	if ((file = mmap(NULL, get_entry_size(entry), PROT_WRITE,
			 MAP_FILE | MAP_SHARED, fd, 0)) == MAP_FAILED)
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
	prog_bar_init(&bar, title, get_entry_size(entry),
		      (struct timespec){ 0, 100000000 });

	while (total_received < get_entry_size(entry)) {
		if ((recv_size = recv(client->socket, file + total_received,
				      (get_entry_size(entry) - total_received),
				      0)) < 0) {
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

	if (munmap(file, get_entry_size(entry)) < 0)
		ERR("munmap");
	if (close(fd) < 0)
		ERR("close");
}

void cleanup_client(client_t *client)
{
	if (close(client->socket) < 0)
		ERR("close");

	free(client->name);

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
		entry_t entry;
		char path[PATH_MAX];

		accept_client(sock, &client);
		if (!confirm_transfer(&client, &entry, path)) {
			cleanup_client(&client);
			continue;
		}

		strcpy(path, downloads_directory);
		strcat(path, "/");
		strcat(path, get_entry_name(&entry));

		recv_entry_data(&client, &entry, path);
		cleanup_client(&client);
	}

	close(sock);

	return EXIT_SUCCESS;
}
