#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEFAULT_PORT 2137

int open_and_map_file(void **file, int *fd, file_data_t *file_data, const char *path)
{
	if ((*fd = open(path, O_RDONLY)) < 0) {
        perror("file open");
        return -1;
	}

    if (read_file_data_from_fd(file_data, path, *fd) < 0) {
        goto fd_cleanup;
    }

	*file = mmap(NULL, file_data->size, PROT_READ,
			  MAP_FILE | MAP_SHARED, *fd, 0);

    if (*file == MAP_FAILED) {
        perror("mmap");
        goto fd_cleanup;
    }

    return 0;

fd_cleanup:
    close(*fd);
    return -1;
}

int server_connect(char *ip_str)
{
	int ret;
	struct in_addr target_ip;
	if ((ret = inet_pton(AF_INET, ip_str, &target_ip)) < 1) {
		fprintf(stderr, "wrong ip: %s\n", ip_str);
		return -1;
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

int send_init_data(int soc, file_data_t *file_data)
{
	send_all(file_data, sizeof(file_data_t), soc);

	char buf[16] = { 0 };
	if (recv(soc, buf, 16, 0) < 1) {
		return -1;
	}

	if (strcmp(buf, "start") != 0) {
		fprintf(stderr, "wrong string received: %s\n", buf);
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
    int ret = EXIT_SUCCESS;
    if (argc < 3) {
        fprintf(stderr, "USAGE: %s IP FILEPATH", argv[0]);
        return EXIT_FAILURE;
    }

    file_data_t file_data;
    int file_fd;
    void *file;

    if (open_and_map_file(&file, &file_fd, &file_data, argv[2]) < 0)
        return EXIT_FAILURE;

    int soc = server_connect(argv[1]);
    if (soc < 0) {
        ret = EXIT_FAILURE;
        goto file_cleanup;
    }

    file_size_t size = bytes_to_size(file_data.size);
    printf("sending %s, size %lf%s\n", file_data.name, size.size, file_size_units[size.unit_idx]);

    if (send_init_data(soc, &file_data) < 0) {
        fprintf(stderr, "could not send data to the server\n");
        ret = EXIT_FAILURE;
        goto soc_cleanup;
    }

    if (send_all(file, file_data.size, soc) < 0) {
        perror("sending file");
        ret = EXIT_FAILURE;
        goto soc_cleanup;
    }

soc_cleanup:
    close(soc);

file_cleanup:
    close(file_fd);
    munmap(file, file_data.size);

    return ret;
}

