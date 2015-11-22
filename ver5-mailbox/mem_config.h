#include <linux/kernel.h>
#include <linux/slab.h>

#define REMOVE 0

/* Configurations */
#define MEMORY_MAJOR 60 // device major number

#define DEBUG 1 // set for debugging output
#define USE_RDMA 0 // set to use RDMA

#define NNODES 3 // # of nodes

#define NSLABS 4096 // # of slabs
#define NSPS_SLAB 8 // # of superpages per slab
#define NPAGES_SP 128 // # of pages per superpage
#define NPAGES_SLAB (NPAGES_SP * NSPS_SLAB) // # of pages per slab
#define NPAGES (NPAGES_SP * NSPS_SLAB * NSLABS) // total # of pages

#define CHUNK_SIZE 128 // number of slab/pgoff pairs per sync. comm.

#define NODEIP_CONFIG_PATH "/media/nfs/mem_driver/ver5-mailbox/nodeip.conf"
#define PORT_START 49152 // port for RDMA
