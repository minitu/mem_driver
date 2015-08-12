#include <linux/kernel.h>
#include <linux/slab.h>

#define REMOVE 0

/* Configurations */
#define MEMORY_MAJOR 60 // device major number

#define DEBUG 1 // set for debugging output
#define USE_RDMA 1 // set to use RDMA

#define NNODES 2 // number of nodes
#define NSLABS 128 // number of slabs
#define NPAGES_SLAB 1024 // number of pages per slab
#define NPAGES (NPAGES_SLAB*NSLABS) // total number of pages
#define CHUNK_SIZE 128 // number of slab/pgoff pairs per sync. comm.

#define LOCAL_IP "192.168.1.101" // local node's IPv4 address
#define RDMA_PORT 9999 // port for RDMA

/* Local page structure */
struct local_page {
	unsigned long user_va;
	unsigned int slab_no;
	unsigned int slab_pgoff;
	unsigned long slab_va;
	struct vm_area_struct *vma;
};

/* Additional structure,
	 necessary because every local_page
	 has to belong to 2 different lists
   (local free_list & mmap_area) */
struct lp_node {
	struct list_head list;
	struct local_page *lp;
};

/* Remote page structure */
struct remote_page {
	struct list_head list;
	unsigned int slab_no;
	unsigned int slab_pgoff;
};
