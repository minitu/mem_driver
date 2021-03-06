#include <linux/kernel.h>
#include <linux/slab.h>

#define REMOVE 0

/* Configurations */
#define MEMORY_MAJOR 60 // device major number

#define DEBUG 1 // set for debugging output
#define USE_RDMA 1 // set to use RDMA

#define NNODES 3 // number of nodes
#define NSLABS 4096 // number of slabs
#define NPAGES_SLAB 1024 // number of pages per slab
#define NPAGES (NPAGES_SLAB*NSLABS) // total number of pages
#define CHUNK_SIZE 128 // number of slab/pgoff pairs per sync. comm.

#define NODEIP_CONFIG_PATH "/media/nfs/mem_driver/ver4/nodeip.conf"
#define PORT_START 49152 // port for RDMA
