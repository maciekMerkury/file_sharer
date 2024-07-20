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

int accept_client(int sock)
{
	printf("Waiting for a new client\n");
	int client;
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if ((client = accept(sock, (struct sockaddr *)&addr, &len)) < 0)
		ERR("accept");

	char ip_str[INET_ADDRSTRLEN];
	if (!inet_ntop(AF_INET, &(addr.sin_addr), ip_str, INET_ADDRSTRLEN))
		ERR("inet_ntop");

	printf("client from address %s has connected\n", ip_str);
	return client;
}

bool confirm_transfer(int client, file_data_t *data)
{
	if (recv(client, data, sizeof(file_data_t), 0) < 0)
		ERR("recv");

	char sizes[4][3] = { "B\0", "kB", "MB", "TB" };
	int idx = 0;
	float filesize = data->size;
	while (filesize >= 1024.0f) {
		idx++;
		filesize /= 1024.0f;
	}

	printf("Do you want to receive a file %.255s of %.2f %s [n/Y] ",
	       data->name, filesize, sizes[idx]);

	char s[2];
	fgets(s, 2, stdin);
	char c = s[0];

	return c == 'Y' || c == 'y' || c == '\n';
}

void receive_file(int client, file_data_t data)
{
	send(client, "start", 6, 0);
	printf("receiving file...\n");

	off_t size = data.size;

	struct passwd *pw = getpwuid(getuid());

	char file_path[PATH_MAX + 1];
	snprintf(file_path, PATH_MAX + 1, "%s/Downloads/%s", pw->pw_dir,
		 data.name);

	int fd;
	if ((fd = open(file_path, O_RDWR | O_CREAT | O_APPEND | O_EXCL, 0644)) <
	    0)
		ERR("open");
	if (ftruncate(fd, size) < 0)
		ERR("ftruncate");

	void *file;
	if ((file = mmap(NULL, size, PROT_WRITE, MAP_FILE | MAP_SHARED, fd,
			 0)) == MAP_FAILED)
		ERR("mmap");

	ssize_t recv_size;
	while (size > 0) {
		if ((recv_size = recv(client, file, size, 0)) < 0)
			ERR("recv");
		size -= recv_size;
		file += recv_size;
	}
	printf("Done receiving file\n");
}

void disconnect_client(int client)
{
	close(client);
	printf("Disconnected the client\n");
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

	while (true) {
		int client = accept_client(sock);
		file_data_t data;
		bool result = confirm_transfer(client, &data);

		if (!result) {
			disconnect_client(client);
			continue;
		}

		receive_file(client, data);
		disconnect_client(client);
	}

	close(sock);

	return EXIT_SUCCESS;
}
