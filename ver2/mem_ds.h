#include "mem_driver.h"
#include <linux/hashtable.h>
#include <linux/rbtree.h>

/* Hash table sizes */
#define RM_HT_BITS 20 // 2^(RM_HT_BITS)
#define MA_HT_BITS 10 // 2^(MA_HT_BITS)

struct remote_map {
	struct hlist_node hlist;
	void *user_va;
	unsigned int node;
	struct remote_page *rp;
};

struct mmap_area {
	struct hlist_node hlist;
	void *mmap_va;
	unsigned long npages;
	void *vma;
	struct list_head list; // list of lp_nodes (for eviction)
	struct rb_root rbtree; // red-black tree of local/remote pages (for remote checking)
};

struct page_node {
	struct rb_node rbnode;
	void *user_va;
	struct local_page *lp;
};

void rm_ht_init(void);
void ma_ht_init(void);
struct remote_map *rm_ht_get(void *user_va);
struct mmap_area *ma_ht_get(void *mmap_va);
int rm_ht_add(void *user_va, unsigned int node, struct remote_page *rp);
int rm_ht_del(void *user_va);
int ma_ht_add_mmap(void *mmap_va, int npages, void* vma);
int ma_ht_add_page(void *mmap_va, void *user_va, struct local_page *lp);
int ma_ht_change_lp(void *mmap_va, void *user_va, struct local_page *lp);
int ma_ht_del_mmap(void *mmap_va);
void rm_ht_destroy(void);
void ma_ht_destroy(void);
