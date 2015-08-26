#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "mem_ioctl.h"

#define NDIM 128
#define REMOTE 1

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
	int* b;
	int* c;
	int i, j, k;
	int fd;

	struct timeval start, end, diff;
	gettimeofday(&start, NULL);

#if(!REMOTE)
	// initialize matrices
	a = (int*)malloc(sizeof(int) * NDIM * NDIM);
	b = (int*)malloc(sizeof(int) * NDIM * NDIM);
	c = (int*)malloc(sizeof(int) * NDIM * NDIM);
	for (i = 0; i < NDIM; i++) {
		for (j = 0; j < NDIM; j++) {
			a[NDIM * i + j] = 2;
			b[NDIM * i + j] = 3;
		}
	}

	// multiply
	for (i = 0; i < NDIM; i++) {
		for (j = 0; j < NDIM; j++) {
			for (k = 0; k < NDIM; k++) {
				c[NDIM * i + j] = a[NDIM * i + k] * b[NDIM * k + j];
			}
		}
	}
#else
	// open device
	fd = open("/dev/memory", O_RDWR);
	if(fd ==-1) {
		perror("error: open /dev/memory\n");
		return 0;
	}

	// initialize matrices
	a = (int*)mmap(NULL, sizeof(int) * NDIM * NDIM, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	b = (int*)mmap(NULL, sizeof(int) * NDIM * NDIM, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	c = (int*)mmap(NULL, sizeof(int) * NDIM * NDIM, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	for (i = 0; i < NDIM; i++) {
		for (j = 0; j < NDIM; j++) {
			a[NDIM * i + j] = 2;
			b[NDIM * i + j] = 3;
		}
	}

	// multiply
	for (i = 0; i < NDIM; i++) {
		for (j = 0; j < NDIM; j++) {
			for (k = 0; k < NDIM; k++) {
				c[NDIM * i + j] = a[NDIM * i + k] * b[NDIM * k + j];
			}
		}
	}

	// close device
	close(fd);
#endif

	gettimeofday(&end, NULL);
	timeval_subtract(&diff, &end, &start);
	printf("elapsed time: %ld.%ld\n", diff.tv_sec, diff.tv_usec);
	
/*
	int fd;
	
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

	close(fd);
	*/

	return 0;
}
