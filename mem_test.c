#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "mem_ioctl.h"

int my_munmap(int fd, void *addr, size_t length) {
	struct munmap_info info;
	info.addr = addr;
	info.length = length;
	
	ioctl(fd, 7, &info);
	return munmap(addr, length);
}

int main()
{
	printf("sizeof(void*): %d\n", sizeof(void*));
	printf("sizeof(unsigned long): %d\n", sizeof(unsigned long));

	int fd;
	fd = open("/dev/memory", O_RDWR);
	if(fd ==-1) {
		perror("error: open /dev/memory\n");
		return 0;
	}
/*
	int** test_addr[2];
	test_addr[0] = (int*)mmap(NULL, sizeof(int) * 1024 * 3, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	test_addr[1] = (int*)mmap(NULL, sizeof(int) * 1024 * 3, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	test_addr[0][0] = 0;
	test_addr[0][1024] = 0;
	test_addr[0][2048] = 0;
	test_addr[1][0] = 0;
*/
	close(fd);

	return 0;
}
