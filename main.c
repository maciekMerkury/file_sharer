#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "core.h"

#define DEFAULT_PORT 2137

ssize_t send_all(const void *const buf, size_t len, int soc)
{
    size_t sent = 0;

    while (sent < len) {
        const ssize_t s = send(soc, (void*)((uintptr_t)buf + sent), len - sent, 0);
        if (s < 0)
            return s;

        sent += s;
    }

    return sent;
}

int server_connect(char *ip_str)
{
	int ret;
	struct in_addr target_ip;
	if ((ret = inet_pton(AF_INET, ip_str, &target_ip)) < 1) {
		fprintf(stderr, "wrong ip: %s\n", ip_str);
	}

	int soc;
	if ((soc = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
		return -1;
	}

	struct sockaddr_in addr = { .sin_addr = target_ip,
				    .sin_family = AF_INET,
				    .sin_port = htons(DEFAULT_PORT),
				    .sin_zero = { 0 } };

	if ((ret = connect(soc, (const struct sockaddr *)&addr,
			   sizeof(struct sockaddr_in)) < 0)) {
		perror("connect");
		close(soc);
		return -2;
	}

	return soc;
}

int main(int argc, char **argv)
{
	int ret = 0;
	if (argc < 3) {
		fprintf(stderr, "argc < 3\n");
		return EXIT_FAILURE;
	}

	file_data_t file_info;
	if ((ret = read_file_data(&file_info, argv[2])) < 0) {
        perror("read file data");
		return EXIT_FAILURE;
    }

	int soc = server_connect(argv[1]);
	if (soc < 0)
		return EXIT_FAILURE;

    printf("name: %s, size %ld\n", file_info.name, file_info.size);
    send_all(&file_info, sizeof(file_data_t), soc);

    char buf[16] = { 0 };
    if ((ret = recv(soc, buf, 16, 0)) < 1) {
        fprintf(stderr, "recv 2 len is %i\n", ret);
        ret = EXIT_FAILURE;
        goto soc_cleanup;
    }

    if (strcmp(buf, "start") != 0) {
        fprintf(stderr, "wrong string received: %s\n", buf);
        ret = EXIT_FAILURE;
        goto soc_cleanup;
    }

    int fd;
    if ((fd = open(argv[2], O_RDONLY)) < 0) {
        perror("open");
        goto soc_cleanup;
    }

    void *file = mmap(NULL, file_info.size, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);

    send_all(file, file_info.size, soc);
    munmap(file, file_info.size);

    close(fd);
soc_cleanup:
	close(soc);
	return ret;
}

