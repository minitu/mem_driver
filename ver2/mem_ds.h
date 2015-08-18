#include "mem_driver.h"
#include <linux/list.h>
#include <linux/hashtable.h>

/* Hash table sizes */
#define RM_HT_BITS 20 // 2^(RM_HT_BITS)
#define MA_HT_BITS 10 // 2^(MA_HT_BITS)

/* Data structures */
struct local_page {
	struct list_head list;
	unsigned long user_va;
	unsigned int slab_no;
	unsigned int slab_pgoff;
	unsigned long slab_va;
	struct vm_area_struct *vma;
};

struct remote_page {
	struct list_head list;
	unsigned int node;
	unsigned int slab_no;
	unsigned int slab_pgoff;
};

struct remote_map {
	struct hlist_node hlist;
	unsigned long user_va;
	struct remote_page *rp;
};

struct mmap_area {
	struct hlist_node hlist;
	unsigned long mmap_va;
	unsigned long npages;
	void *vma;
	struct list_head local_alloc_list;
	struct list_head remote_free_list;
	struct list_head remote_alloc_list;
};

/* Function declarations */
void rm_ht_init(void);
void ma_ht_init(void);
struct remote_map *rm_ht_get(unsigned long user_va);
struct mmap_area *ma_ht_get(unsigned long mmap_va);
int rm_ht_add(unsigned long user_va, struct remote_page *rp);
int rm_ht_del(unsigned long user_va);
int ma_ht_add(unsigned long mmap_va, int npages, void* vma);
int ma_ht_del(unsigned long mmap_va);
void rm_ht_destroy(void);
void ma_ht_destroy(void);
