#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "mem_ioctl.h"

/*
int my_munmap(struct file *filp, void *addr, size_t length) {
	ioctl(filp, 5, 0);
	return munmap(addr, length);
}
*/

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
	else {
	}

	//ioctl(fd, 5, 0);
	
	int* test_addr[2];
	test_addr[0] = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	/*
	test_addr[1] = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	my_munmap(fd, test_addr[0], sizeof(int));
	munmap(test_addr[1], sizeof(int));
	*/

	/*
	test_addr[0][0] = 1;
	printf("test_addr[0][0]: %d\n", test_addr[0][0]);
	test_addr[1] = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	test_addr[0][0] = 2;
	printf("test_addr[0][0]: %d\n", test_addr[0][0]);
	*/

	/*
	int size = sizeof(int) * 1024 + sizeof(int);
	unsigned long size = (unsigned long)10240 * (unsigned long)1024 * \
											 (unsigned long)1024 * (unsigned long)1024;
	int* addr[10];
	int i;
	for (i = 0; i < 10; i++) {
		addr[i] = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if(addr[i] == MAP_FAILED) {
			printf("addr[%d]=%p\n", i, addr);
			perror("MAP_ERROR");
			return -1;
		}
	}
	
	addr[0][0] = 1;
	printf("addr[0][0]: %d\n", addr[0][0]);
	addr[0][1] = 2;
	printf("addr[0][1]: %d\n", addr[0][1]);
	addr[0][1024] = 3;
	printf("addr[0][1024]: %d\n", addr[0][1024]);

	addr[0][0] = 1;
	printf("addr[0]: %d\n", addr[0][0]);
	addr[0][1] = 2;
	printf("addr[1]: %d\n", addr[0][1]);
	addr[0][1024] = 3;
	printf("addr[1024]: %d\n", addr[0][1024]);
	printf("addr[0]: %d\n", addr[0][0]);
	unsigned long t = (unsigned long)8 * (unsigned long)1024 * (unsigned long)1024 
		* (unsigned long)1024 * (unsigned long)1024;
	*(addr[8] + t) = 1;

	int ret;
	for (i = 0; i < 10; i++) {
		ret = munmap(addr[i], size);
		if (ret != 0) {
			printf("munmap error!\n");
			return -1;
		}
	}
	*/

	close(fd);

	return 0;
}
