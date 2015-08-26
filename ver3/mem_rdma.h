#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/pci-dma.h>
#include <asm/atomic.h>
#include <linux/scatterlist.h>
#include <linux/kthread.h>

#include "mem_config.h"

/* RDMA semantics borrowed heavily from krping */
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

/* Max buffer size for I/O */
#define MEMORY_BUFSIZE	PAGE_SIZE
#define MEMORY_SQ_DEPTH	64

#define htonll(x) cpu_to_be64((x))
#define ntohll(x) cpu_to_be64((x))

/* States used to signal events between completion handler
	 and the client or server */
enum test_state {
	IDLE = 1,
	CONNECT_REQUEST, // 2
	ADDR_RESOLVED, // 3
	ROUTE_RESOLVED, // 4
	CONNECTED, // 5
	COMM_READY, // 6
	COMM_COMPLETE, // 7
	ERROR // 8
};

/* RDMA info */
struct memory_rdma_info {
	uint64_t slabs[NSLABS];
	uint32_t rkey;
	uint32_t size;
};

/* Synchronous comm info */
struct memory_comm_info {
	uint32_t req_type;
	uint64_t munmap_va;
	uint32_t slab[CHUNK_SIZE];
	uint32_t pgoff[CHUNK_SIZE];
	uint32_t cnt;
};

/* RDMA control block struct */
struct memory_cb {
	//int server;						// 0 iff client
	struct ib_cq *cq;				// completion queue
	struct ib_pd *pd;				// protection domain
	struct ib_qp *qp;				// queue pair

	struct ib_mr *dma_mr;  // memory registration

	enum test_state state;
	wait_queue_head_t sem;

	uint16_t port;					// dst port
	u8 addr[16];					// dst addr
	char *addr_str;					// dst addr string
	uint8_t addr_type;				// ADDR_FAMILY - IPv4/v6

	int size;						// packet size
	int txdepth;					// SQ depth

	struct ib_recv_wr rq_wr;		// recv work request record
	struct ib_sge recv_sgl;			// recv single SGE (scatter/gather element)
	struct memory_rdma_info recv_buf;
	u64 recv_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(recv_mapping)
	struct memory_comm_info recv_comm_buf;
	u64 recv_comm_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(recv_comm_mapping)
	
	struct ib_send_wr sq_wr;		// send work request record
	struct ib_sge send_sgl;			// send single SGE
	struct memory_rdma_info send_buf;
	u64 send_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(send_mapping)
	struct memory_comm_info send_comm_buf;
	u64 send_comm_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(send_comm_mapping)

	struct ib_send_wr rdma_sq_wr;	// RDMA work request record
	struct ib_sge rdma_sgl;			// RDMA single SGE
	/*
	void* rdma_buf;
	u64 rdma_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(rdma_mapping)
	*/
	struct scatterlist rdma_sl[NSLABS]; // RDMA scatterlist

	u64 local_addr[NSLABS]; // local addresses of slabs

	uint32_t remote_rkey;
	uint64_t remote_addr[NSLABS];
	uint32_t remote_len;

	uint32_t req_type;
	uint64_t munmap_va;
	uint32_t slab[CHUNK_SIZE];
	uint32_t pgoff[CHUNK_SIZE];
	uint32_t cnt;

	/* Connection manager */
	struct rdma_cm_id *cm_id;		// connection on client, listener on server
	struct rdma_cm_id *child_cm_id;	// connection on server side
};

int memory_rdma_init(void);
void memory_rdma_exit(void);
int server_rdma_write(unsigned int local_slab, unsigned int local_pgoff, \
		unsigned int node, unsigned int remote_slab, unsigned int remote_pgoff);
int server_rdma_read(unsigned int local_slab, unsigned int local_pgoff, \
		unsigned int node, unsigned int remote_slab, unsigned int remote_pgoff);
//int server_ask_free(unsigned int *slab, unsigned int *pgoff);
//int server_tell_free(unsigned long munmap_va, unsigned int *slabs, unsigned int *pgoffs, unsigned int cnt);
