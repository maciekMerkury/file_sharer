#include <assert.h>
#include <poll.h>
#include <linux/limits.h>
#include <dirent.h>
#include <poll.h>
#include <time.h>
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

	mt = mt_ack;
	exchange_data_with_socket(client->socket, op_write, &mt,
				  sizeof(message_type), NULL);

	return true;
}

void recv_entry(client_t *client, entry_t *entry)
{
	size_t s;
	if (recv(client->socket, &s, sizeof(size_t), 0) < 0)
		ERR("recv");

	void *data = malloc(s);
	if (recv(client->socket, data, s, 0) < 0)
		ERR("recv");

	inflate_entry(entry, data);
	free(data);
	assert(entry->type == mt_file || entry->type == mt_dir);
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

	printf("Client %s from address %s has connected\n", client->name,
	       client->addr_str);

	return true;
}

bool confirm_transfer(client_t *client, entry_t *entry, char path[PATH_MAX])
{
	recv_entry(client, entry);

	size_info size = bytes_to_size(entry->size);
	printf("Do you want to receive %s `%.255s` of size %.2lf %s"
	       " from user %s at host %s [Y/n] ",
	       get_entry_type_name(entry), entry->name, size.size, unit(size),
	       client->name, client->addr_str);

	char *line = NULL;
	size_t len;
	if (getline(&line, &len, stdin) < 0)
		ERR("getline");
	char c = line[0];
	free(line);

	return c == 'y' || c == 'Y' || c == '\n';
}

void recv_file_data(client_t *client, size_t size, struct file_data file_data,
		    char path[PATH_MAX])
{
	int fd;
	if ((fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_EXCL,
		       file_data.permissions)) < 0)
		ERR("open");
	if (ftruncate(fd, size) < 0)
		ERR("ftruncate");

	void *file;
	if ((file = mmap(NULL, size, PROT_WRITE, MAP_FILE | MAP_SHARED, fd,
			 0)) == MAP_FAILED)
		ERR("mmap");

	progress_bar_t bar;
	char title[PATH_MAX];
	snprintf(title, PATH_MAX, "Receiving %s", path);
	prog_bar_init(&bar, title, size, (struct timespec){ 0, 100000000 });

	ssize_t l = exchange_data_with_socket(client->socket, op_read, file,
					      size, &bar);

	assert(l == size);

	if (munmap(file, size) < 0)
		ERR("munmap");
	if (close(fd) < 0)
		ERR("close");
}

void recv_dir_data(client_t *client, size_t size, struct dir_data file_data,
		   char path[PATH_MAX])
{
}

void recv_entry_data(client_t *client, entry_t *entry, char path[PATH_MAX])
{
	message_type mt = mt_ack;
	if (send(client->socket, &mt, sizeof(message_type), 0) < 0)
		ERR("send");

	switch (entry->type) {
	case mt_file:
		recv_file_data(client, entry->size, entry->data.file, path);
		break;
	case mt_dir:
		recv_dir_data(client, entry->size, entry->data.dir, path);
		break;
	default:
		assert(false);
		break;
	}
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

void read_args(int argc, char *argv[], uint16_t *port,
	       char downloads_directory[PATH_MAX])
{
	if (argc < 3)
		usage(argv[0]);

	*port = atoi(argv[1]);

	realpath(argv[2], downloads_directory);

	DIR *dir;
	if ((dir = opendir(downloads_directory)) == NULL)
		ERR("opendir");
	if (closedir(dir) < 0)
		ERR("closedir");
}

int main(int argc, char *argv[])
{
	char downloads_directory[PATH_MAX];
	uint16_t port;

	read_args(argc, argv, &port, downloads_directory);

	int sock = setup(port);

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
		strcat(path, entry.name);

		recv_entry_data(&client, &entry, path);
		cleanup_client(&client);
		entry_deallocate(&entry);
	}

	close(sock);

	return EXIT_SUCCESS;
}
