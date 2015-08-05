#include "mem_hash.h"

static struct remote_map *rm_ht[RM_HT_SIZE];
static struct mmap_page *mp_ht[MP_HT_SIZE];

void rm_ht_init(void) {
	
	unsigned long i;

	for (i = 0; i < RM_HT_SIZE; i++) {
		rm_ht[i] = NULL;
	}
}

void mp_ht_init(void) {
	
	unsigned long i;
	
	for (i = 0; i < MP_HT_SIZE; i++) {
		mp_ht[i] = NULL;
	}
}

unsigned long rm_ht_hash(unsigned long user_va) {

	return user_va % RM_HT_SIZE;
}

unsigned long mp_ht_hash(unsigned long mmap_va) {

	return mmap_va % MP_HT_SIZE;
}

struct remote_map *rm_ht_get(unsigned long user_va) {

	struct remote_map *rm;

	for (rm = rm_ht[rm_ht_hash(user_va)]; rm != NULL; rm = rm->next) {
		if (user_va == rm->user_va)
			return rm;
	}
	
	return NULL;
}

struct mmap_page *mp_ht_get(unsigned long mmap_va) {
	
	struct mmap_page *mp;

	for (mp = mp_ht[mp_ht_hash(mmap_va)]; mp != NULL; mp = mp->next) {
		if (mmap_va == mp->mmap_va)
			return mp;
	}
	
	return NULL;
}

int rm_ht_put(unsigned int valid, unsigned long user_va, unsigned int node, \
		unsigned int slab_no, unsigned int slab_pgoff, struct local_page *lp) {

	struct remote_map *rm;
	unsigned long hashval;

	if ((rm = rm_ht_get(user_va)) == NULL) {
		rm = (struct remote_map *) kmalloc(sizeof(struct remote_map), GFP_KERNEL);
		if (rm == NULL)
			return -1;
		hashval = rm_ht_hash(user_va);
		rm->next = rm_ht[hashval];
		rm->valid = 1;
		rm->user_va = user_va;
		rm->node = node;
		rm->slab_no = slab_no;
		rm->slab_pgoff = slab_pgoff;
		rm->lp = lp;
		rm_ht[hashval] = rm;
	}
	else {
		if (valid == 0) {
			rm->valid = 0;
		}
		else {
			rm->valid = 1;
			rm->node = node;
			rm->slab_no = slab_no;
			rm->slab_pgoff = slab_pgoff;
			rm->lp = lp;
		}
	}

	return 0;
}

int mp_ht_add_mmap(unsigned long mmap_va, int npages, void* vma) {

	if (mp_ht_get(mmap_va) == NULL) {
		struct mmap_page *mp = (struct mmap_page *)kmalloc(sizeof(struct mmap_page), GFP_KERNEL);
		if (mp == NULL)
			return -1;
		unsigned long hashval = mp_ht_hash(mmap_va);
		mp->next = mp_ht[hashval];
		mp->mmap_va = mmap_va;
		mp->npages = npages;
		mp->vma = vma;
		INIT_LIST_HEAD(&(mp->list));
		mp_ht[hashval] = mp;
	}

	return 0;
}	

int mp_ht_add_page(unsigned long mmap_va, unsigned long user_va, struct local_page *lp) {

	struct mmap_page *mp = mp_ht_get(mmap_va);
	if (mp == NULL)
		return -1;
	struct page_node *pn = (struct page_node *)kmalloc(sizeof(struct page_node), GFP_KERNEL);
	if (pn == NULL)
		return -1;
	pn->user_va = user_va;
	pn->lp = lp;
	list_add(&(pn->list), &(mp->list));

	return 0;
}

int mp_ht_change_lp(unsigned long mmap_va, unsigned long user_va, struct local_page *lp) {

	struct mmap_page *mp = mp_ht_get(mmap_va);
	if (mp == NULL)
		return -1;
	struct list_head *lh;
	struct page_node *pn;

	if (list_empty(&(mp->list)))
		return -1;
	else {
		for (lh = (mp->list).next; lh != &(mp->list); lh = lh->next) {
			pn = list_entry(lh, struct page_node, list);
			if (pn->user_va == user_va) {
				pn->lp = lp;
				return 0;
			}
		}
	}
	
	return -1;
}

/*
int mp_ht_del_page(unsigned long user_va, int slab_no, int slab_pgoff) {

	struct mmap_page *mp;
	struct list_head *lh;
	struct page_node *pn;

	if ((mp = mp_ht_get(user_va)) == NULL) {
		return -1;
	}
	else {
		if (list_empty(&(mp->list))) {
			return -1;
		}
		else {
			for (lh = (mp->list).next; lh != &(mp->list); lh = lh->next) {
				pn = list_entry(lh, struct page_node, list);
				if ((pn->slab_no == slab_no) && (pn->slab_pgoff == slab_pgoff)) {
					list_del(lh);
					kfree(pn);
					return 0;
				}
			}
		}
	}

	return -1;
}
*/


void rm_ht_destroy(void) {

	struct remote_map *rm, *temp;
	int i;

	for (i = 0; i < RM_HT_SIZE; i++) {
		if ((rm = rm_ht[i]) != NULL) {
			while (rm != NULL) {
				temp = rm;
				rm = rm->next;
				kfree(temp);
			}
		}
	}
}

void mp_ht_destroy(void) {
	
	struct mmap_page *mp, *temp_mp;
	struct list_head *lh, *temp_lh;
	struct page_node *pn;
	int i;

	for (i = 0; i < MP_HT_SIZE; i++) {
		if ((mp = mp_ht[i]) != NULL) {
			while (mp != NULL) {
				temp_mp = mp;
				mp = mp->next;
				if (!list_empty(&(temp_mp->list))) {
					for (lh = (temp_mp->list).next; lh != &(temp_mp->list);) {
						pn = list_entry(lh, struct page_node, list);
						temp_lh = lh;
						lh = lh->next;
						list_del(temp_lh);
						kfree(pn);
					}
				}
				kfree(temp_mp);
			}
		}
	}
}

