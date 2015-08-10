#include <linux/ioctl.h>

#define IOCTL_HASH_TEST _IOR(0xFF, 0, int)

struct munmap_info {
	void *addr;
	size_t length;
};
