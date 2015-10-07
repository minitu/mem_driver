#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "mem_ioctl.h"

int main(int argc, char** argv)
{
	if (argc != 6) {
		printf("usage: ./mem_rdmatest [local_slab] [local_pgoff] [remote_node] [remote_slab] [remote_pgoff]\n");
		return -1;
	}

	int fd;
	struct rdma_test_info info;

	fd = open("/dev/memory", O_RDWR);
	if(fd ==-1) {
		perror("error: open /dev/memory\n");
		return 0;
	}

	info.local_slab = atoi(argv[1]);
	info.local_pgoff = atoi(argv[2]);
	info.remote_node = atoi(argv[3]);
	info.remote_slab = atoi(argv[4]);
	info.remote_pgoff = atoi(argv[5]);

	ioctl(fd, 6, &info);

	close(fd);

	return 0;
}
