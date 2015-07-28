#include "mem_driver.h"

#define HASH_SIZE NPAGES
#define MAX_MMAPS 1024 // Maximum number of mmaps

/* Remote map structure. Only exists for remote pages */
struct remote_map {
	struct remote_map *next; // for collision
	unsigned long user_va; // user's mmapped virtual address
	int node; // which remote node?
	int slab_no; // remote slab number
	int slab_pgoff; // remote page offset in slab
	struct list_node *ln; // pointer to list_node
};

/* Mmapped page structure */
struct mmap_page {
	struct mmap_page *next;
	unsigned long id;
	struct list_head list;
};

struct page_node {
	struct list_head list;
	int slab_no; // only for remote
	int slab_pgoff; // only for remote
	struct list_node *ln; // only for local
};

void rm_ht_init(void);
void mp_ht_init(void);
struct remote_map *rm_ht_get(unsigned long user_va);
struct mmap_page *mp_ht_get(unsigned long id);
int rm_ht_put(unsigned long user_va, int node, int slab_no, int slab_pgoff, struct list_node *ln);
int mp_ht_put(unsigned long id, int slab_no, int slab_pgoff, struct list_node *ln);
int mp_ht_del(unsigned long id, int slab_no, int slab_pgoff);
void rm_ht_destroy(void);
void mp_ht_destroy(void);
