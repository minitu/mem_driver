#include <linux/kernel.h>
#include <linux/slab.h>

#define NSLABS 4096 // number of slabs
#define NPAGES_SLAB 1024 // number of pages per slab (1024)
#define NPAGES (NPAGES_SLAB*NSLABS) // total number of pages
