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
	printf("sizeof(void*): %lu\n", sizeof(void*));
	printf("sizeof(unsigned long): %lu\n", sizeof(unsigned long));

	int fd;
	fd = open("/dev/memory", O_RDWR);
	if(fd ==-1) {
		perror("error: open /dev/memory\n");
		return 0;
	}

	int* test_addr[2];
	test_addr[0] = (int*)mmap(NULL, sizeof(int) * 1024 * 3, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	test_addr[1] = (int*)mmap(NULL, sizeof(int) * 1024 * 3, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	test_addr[0][0] = 0;
	test_addr[0][1024] = 1;
	test_addr[0][2048] = 2;
	test_addr[1][0] = 3;
	test_addr[1][1024] = 4;
	test_addr[1][2048] = 5;
	printf("test_addr[0][0]: %d\n", test_addr[0][0]);
	my_munmap(fd, test_addr[1], sizeof(int) * 1024 * 3);
	printf("test_addr[0][1024]: %d\n", test_addr[0][1024]);
	printf("test_addr[0][2048]: %d\n", test_addr[0][2048]);
	/*
	printf("test_addr[0][0]: %d\n", test_addr[0][0]);
	printf("test_addr[0][1024]: %d\n", test_addr[0][1024]);
	printf("test_addr[0][2048]: %d\n", test_addr[0][2048]);
	printf("test_addr[1][0]: %d\n", test_addr[1][0]);
*/
	/*
	test_addr[0][1024] = 0;
	test_addr[0][2048] = 0;
	test_addr[1][0] = 0;
*/
	close(fd);

	return 0;
}
