#include <linux/kernel.h>
#include <linux/slab.h>

#define MEMORY_MAJOR 60 // device major number
#define NNODES 2 // number of nodes
#define NSLABS 1 // number of slabs
#define NPAGES_SLAB 4 // number of pages per slab (1024)
#define NPAGES (NPAGES_SLAB*NSLABS) // total number of pages
#define CHUNK_SIZE 128 // number of slab/pgoff pairs per sync. comm.
