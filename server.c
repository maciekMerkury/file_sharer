#include <linux/limits.h>
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

#include "core.h"

typedef struct client {
	int socket;
	char addr_str[INET_ADDRSTRLEN];
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

	printf("client from address %s has connected\n", client->addr_str);
}

bool confirm_transfer(client_t *client, file_data_t *data, char path[PATH_MAX])
{
	if (recv(client->socket, data, sizeof(file_data_t), 0) < 0)
		ERR("recv");

	file_size_t size = bytes_to_size(data->size);

	printf("Host %s wants to send you file `%.255s` of size %.2f %s\n",
	       client->addr_str, data->name, size.size,
	       file_size_units[size.unit_idx]);

	printf("Specify a download directory (Ctrl+D to refuse transfer): ");

	char buf[PATH_MAX + 1];
	char *ret = fgets(buf, PATH_MAX + 1, stdin);

	if (ret == NULL) {
		clearerr(stdin);
		printf("\n");
		return false;
	}

	char *newl = strchr(buf, '\n');
	if (newl)
		*newl = '\0';

	expand_bash_path(path, buf);

	strcat(path, data->name);

	return true;
}

void receive_file(client_t *client, file_data_t data, char path[PATH_MAX])
{
	send(client->socket, "start", 6, 0);
	printf("receiving file...\n");

	off_t size = data.size;

	int fd;
	if ((fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_EXCL, 0644)) < 0)
		ERR("open");
	if (ftruncate(fd, size) < 0)
		ERR("ftruncate");

	void *file;
	if ((file = mmap(NULL, size, PROT_WRITE, MAP_FILE | MAP_SHARED, fd,
			 0)) == MAP_FAILED)
		ERR("mmap");

	ssize_t recv_size;
	while (size > 0) {
		if ((recv_size = recv(client->socket, file, size, 0)) < 0)
			ERR("recv");
		size -= recv_size;
		file += recv_size;
	}
	printf("Done receiving file\n");
}

void disconnect_client(client_t *client)
{
	close(client->socket);
	printf("Disconnected client %s\n", client->addr_str);
}

void usage(char *prog)
{
	fprintf(stderr, "USAGE: %s port\n", prog);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	if (argc < 2)
		usage(argv[0]);

	uint16_t port = atoi(argv[1]);

	int sock = setup(port);

	client_t client;
	file_data_t data;
	char path[PATH_MAX];

	while (true) {
		accept_client(sock, &client);
		bool result = confirm_transfer(&client, &data, path);

		if (!result) {
			disconnect_client(&client);
			continue;
		}

		receive_file(&client, data, path);
		disconnect_client(&client);
	}

	close(sock);

	return EXIT_SUCCESS;
}
