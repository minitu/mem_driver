#include "mem_hash.h"

static struct remote_map *rm_ht[HASH_SIZE];
static struct mmap_page *mp_ht[MAX_MMAPS];

void rm_ht_init(void) {
	
	unsigned long i;

	for (i = 0; i < HASH_SIZE; i++) {
		rm_ht[i] = NULL;
	}
}

void mp_ht_init(void) {
	
	unsigned long i;
	
	for (i = 0; i < MAX_MMAPS; i++) {
		mp_ht[i] = NULL;
	}
}

unsigned long rm_ht_hash(unsigned long user_va) {

	return user_va % HASH_SIZE;
}

unsigned long mp_ht_hash(unsigned long id) {

	return id % MAX_MMAPS;
}

struct remote_map *rm_ht_get(unsigned long user_va) {

	struct remote_map *np;

	for (np = rm_ht[rm_ht_hash(user_va)]; np != NULL; np = np->next) {
		if (user_va == np->user_va)
			return np;
	}
	
	return NULL;
}

struct mmap_page *mp_ht_get(unsigned long id) {
	
	struct mmap_page *np;

	for (np = mp_ht[mp_ht_hash(id)]; np != NULL; np = np->next) {
		if (id == np->id)
			return np;
	}
	
	return NULL;
}

int rm_ht_put(unsigned long user_va, int node, int slab_no, int slab_pgoff, struct list_node *ln) {

	struct remote_map *np;
	unsigned long hashval;

	if ((np = rm_ht_get(user_va)) == NULL) {
		np = (struct remote_map *) kmalloc(sizeof(struct remote_map), GFP_KERNEL);
		if (np == NULL)
			return -1;
		hashval = rm_ht_hash(user_va);
		np->next = rm_ht[hashval];
		np->user_va = user_va;
		np->node = node;
		np->slab_no = slab_no;
		np->slab_pgoff = slab_pgoff;
		np->ln = ln;
		rm_ht[hashval] = np;
	}
	else {
		np->node = node;
		np->slab_no = slab_no;
		np->slab_pgoff = slab_pgoff;
	}

	return 0;
}

int mp_ht_put(unsigned long id, int slab_no, int slab_pgoff, struct list_node *ln) {

	struct mmap_page *np = mp_ht_get(id);
	struct page_node *temp;
	unsigned long hashval;

	if (np == NULL) { // first insert of id
		np = (struct mmap_page *) kmalloc(sizeof(struct mmap_page), GFP_KERNEL);
		if (np == NULL)
			return -1;
		hashval = mp_ht_hash(id);
		np->next = mp_ht[hashval];
		np->id = id;
		INIT_LIST_HEAD(&(np->list));
		temp = (struct page_node *) kmalloc(sizeof(struct page_node), GFP_KERNEL);
		if (temp == NULL)
			return -1;
		temp->slab_no = slab_no;
		temp->slab_pgoff = slab_pgoff;
		temp->ln = ln;
		list_add(&(temp->list), &(np->list));
		mp_ht[hashval] = np;
	}
	else { // add new page
		temp = (struct page_node *) kmalloc(sizeof(struct page_node), GFP_KERNEL);
		if (temp == NULL)
			return -1;
		temp->slab_no = slab_no;
		temp->slab_pgoff = slab_pgoff;
		temp->ln = ln;
		list_add(&(temp->list), &(np->list));
	}

	return 0;
}

int mp_ht_del(unsigned long id, int slab_no, int slab_pgoff) {

	struct mmap_page *np;
	struct list_head *lh;
	struct page_node *pn;

	if ((np = mp_ht_get(id)) == NULL) {
		return -1;
	}
	else {
		if (list_empty(&(np->list))) {
			return -1;
		}
		else {
			for (lh = (np->list).next; lh != &(np->list); lh = lh->next) {
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


void rm_ht_destroy(void) {

	struct remote_map *np, *temp;
	int i;

	for (i = 0; i < HASH_SIZE; i++) {
		if ((np = rm_ht[i]) != NULL) {
			while (np != NULL) {
				temp = np;
				np = np->next;
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

	for (i = 0; i < MAX_MMAPS; i++) {
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

