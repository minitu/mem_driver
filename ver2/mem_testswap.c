#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "mem_ioctl.h"

#define REMOTE 0

int my_munmap(int fd, void *addr, size_t length) {
	struct munmap_info info;
	info.addr = addr;
	info.length = length;
	
	ioctl(fd, 7, &info);
	return munmap(addr, length);
}

int timeval_subtract(struct timeval *result, struct timeval *x, \
		struct timeval *y) {

	if (x->tv_usec < y->tv_usec) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_usec - y->tv_usec > 1000000) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;
	}

	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;

	return x->tv_sec < y->tv_sec;
}

int main()
{
	int* a;
	unsigned long i;
	unsigned long NINT = 1024UL * 1024UL * 1024UL;
	int fd;

	struct timeval start, end, diff;
	gettimeofday(&start, NULL);

#if(!REMOTE)
	a = (int*)malloc(sizeof(int) * NINT);
	for (i = 0; i < NINT; i++) {
		a[i] = 7;
	}
#else
	// open device
	fd = open("/dev/memory", O_RDWR);
	if(fd ==-1) {
		perror("error: open /dev/memory\n");
		return 0;
	}

	// initialize matrices
	a = (int*)mmap(NULL, sizeof(int) * NINT, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	for (i = 0; i < NINT; i++) {
		a[i] = 7;
	}

	// close device
	close(fd);
#endif

	gettimeofday(&end, NULL);
	timeval_subtract(&diff, &end, &start);
	printf("elapsed time: %ld.%ld\n", diff.tv_sec, diff.tv_usec);

	return 0;
}
