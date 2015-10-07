#include <linux/ioctl.h>

#define IOCTL_HASH_TEST _IOR(0xFF, 0, int)

struct munmap_info {
	void *addr;
	size_t length;
};

struct rdma_test_info {
	unsigned int local_slab;
	unsigned int local_pgoff;
	unsigned int remote_node;
	unsigned int remote_slab;
	unsigned int remote_pgoff;
};
