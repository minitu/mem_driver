#include "mem_driver.h"

#define RM_HT_SIZE NPAGES
#define MP_HT_SIZE 1024

/* Remote map structure. Only exists for remote pages */
struct remote_map {
	struct remote_map *next; // for collision
	unsigned int valid; 
	unsigned long user_va;
	unsigned int node;
	unsigned int slab_no;
	unsigned int slab_pgoff;
	struct local_page *lp;
};

/* Mmapped page structure */
struct mmap_page {
	struct mmap_page *next; // for collision
	unsigned long mmap_va;
	unsigned long npages;
	void *vma;
	struct list_head list;
};

struct page_node {
	struct list_head list;
	unsigned long user_va;
	struct local_page *lp;
};

void rm_ht_init(void);
void mp_ht_init(void);
struct remote_map *rm_ht_get(unsigned long user_va);
struct mmap_page *mp_ht_get(unsigned long mmap_va);
int rm_ht_put(unsigned int valid, unsigned long user_va, unsigned int node, unsigned int slab_no, unsigned int slab_pgoff, struct local_page *lp);
int mp_ht_add_mmap(unsigned long mmap_va, int npages, void* vma);
int mp_ht_add_page(unsigned long mmap_va, unsigned long user_va, struct local_page *lp);
int mp_ht_change_lp(unsigned long mmap_va, unsigned long user_va, struct local_page *lp);
void rm_ht_destroy(void);
void mp_ht_destroy(void);
