#include "mem_ds.h"

extern struct list_head free_list[NNODES];

DEFINE_HASHTABLE(rm_ht, RM_HT_BITS);
DEFINE_HASHTABLE(ma_ht, MA_HT_BITS);

void rm_ht_init(void) {

	hash_init(rm_ht);
}

void ma_ht_init(void) {
	
	hash_init(ma_ht);
}

struct remote_map *rm_ht_get(unsigned long user_va) {

	struct remote_map *rm;

	hash_for_each_possible(rm_ht, rm, hlist, user_va) {
		if (rm->user_va == user_va) {
			return rm;
		}
	}
	
	return NULL;
}

struct mmap_area *ma_ht_get(unsigned long mmap_va) {
	
	struct mmap_area *ma;

	hash_for_each_possible(ma_ht, ma, hlist, mmap_va) {
		if (ma->mmap_va == mmap_va) {
			return ma;
		}
	}

	return NULL;
}

int rm_ht_add(unsigned long user_va, struct remote_page *rp) {

	struct remote_map *rm;

	rm = (struct remote_map *)kmalloc(sizeof(struct remote_map), GFP_KERNEL);
	if (rm == NULL)
		return -1;

	rm->user_va = user_va;
	rm->rp = rp;

	hash_add(rm_ht, &(rm->hlist), user_va);

	return 0;
}

int rm_ht_del(unsigned long user_va) {

	struct remote_map *rm;

	rm = rm_ht_get(user_va);
	if (rm == NULL)
		return -1;

	hash_del(&(rm->hlist));
	kfree(rm);

	return 0;
}


int ma_ht_add(unsigned long mmap_va, int npages, void* vma) {

	struct mmap_area *ma;

	ma = (struct mmap_area *)kmalloc(sizeof(struct mmap_area), GFP_KERNEL);
	if (ma == NULL)
			return -1;

	ma->mmap_va = mmap_va;
	ma->npages = npages;
	ma->vma = vma;
	INIT_LIST_HEAD(&(ma->local_alloc_list));
	INIT_LIST_HEAD(&(ma->remote_free_list));
	INIT_LIST_HEAD(&(ma->remote_alloc_list));

	hash_add(ma_ht, &(ma->hlist), mmap_va);

	return 0;
}	

int ma_ht_del(unsigned long mmap_va) {

	struct mmap_area *ma;
	struct list_head *lh, *temp_lh;
	struct remote_page *rp;
	
	ma = ma_ht_get(mmap_va);
	if (ma == NULL)
		return -1;

	// move list entries to free lists
	lh = &(ma->local_alloc_list);
	if (!list_empty(lh)) {
		for (lh = lh->next; lh != &(ma->local_alloc_list);) {
			temp_lh = lh;
			lh = lh->next;
			list_del_init(temp_lh);
			list_add(temp_lh, &free_list[0]);
		}
	}
	
	lh = &(ma->remote_free_list);
	if (!list_empty(lh)) {
		for (lh = lh->next; lh != &(ma->remote_free_list);) {
			temp_lh = lh;
			lh = lh->next;
			rp = list_entry(temp_lh, struct remote_page, list);
			list_del_init(temp_lh);
			list_add(temp_lh, &free_list[rp->node]);
		}
	}

	lh = &(ma->remote_alloc_list);
	if (!list_empty(lh)) {
		for (lh = lh->next; lh != &(ma->remote_alloc_list);) {
			temp_lh = lh;
			lh = lh->next;
			rp = list_entry(temp_lh, struct remote_page, list);
			list_del_init(temp_lh);
			list_add(temp_lh, &free_list[rp->node]);
		}
	}
	
	// remove hash table entry
	hash_del(&(ma->hlist));
	kfree(ma);

	return 0;
}

void rm_ht_destroy(void) {

	struct remote_map *rm;
	struct hlist_node *hn;
	int bkt;

	hash_for_each_safe(rm_ht, bkt, hn, rm, hlist) {
		// remove hash table entry
		hash_del(&(rm->hlist));
		kfree(rm);
	}
}

void ma_ht_destroy(unsigned long* mmap_pages_p) {

	struct mmap_area *ma;
	struct hlist_node *hn;
	struct list_head *lh, *temp_lh;
	struct remote_page *rp;
	int bkt;

	hash_for_each_safe(ma_ht, bkt, hn, ma, hlist) {
		// move list entries to free lists
		lh = &(ma->local_alloc_list);
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &(ma->local_alloc_list);) {
				temp_lh = lh;
				lh = lh->next;
				list_del_init(temp_lh);
				list_add(temp_lh, &free_list[0]);
			}
		}
	
		lh = &(ma->remote_free_list);
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &(ma->remote_free_list);) {
				temp_lh = lh;
				lh = lh->next;
				rp = list_entry(temp_lh, struct remote_page, list);
				list_del_init(temp_lh);
				list_add(temp_lh, &free_list[rp->node]);
			}
		}

		lh = &(ma->remote_alloc_list);
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &(ma->remote_alloc_list);) {
				temp_lh = lh;
				lh = lh->next;
				rp = list_entry(temp_lh, struct remote_page, list);
				list_del_init(temp_lh);
				list_add(temp_lh, &free_list[rp->node]);
			}
		}

		// decrease mmap pages
		*mmap_pages_p -= ma->npages;
	
		// remove hash table entry
		hash_del(&(ma->hlist));
		kfree(ma);
	}
}
