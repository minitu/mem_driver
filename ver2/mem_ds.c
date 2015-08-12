#include "mem_ds.h"

DEFINE_HASHTABLE(rm_ht, RM_HT_BITS)
DEFINE_HASHTABLE(ma_ht, MA_HT_BITS)

struct page_node *pn_search(struct rb_root *root, void *user_va) {

	struct rb_node *rbnode = root->rb_node;
	struct page_node *data;
	int result;

	while (node) {
		data = container_of(rbnode, struct page_node, rbnode);

		if (user_va == data->user_va) {
			return data;
		}
		else if (user_va < data->user_va) {
			node = node->rb_left;
		}
		else {
			node = node->rb_right;
		}
	}
	
	return NULL;
}

int pn_insert(struct rb_root *root, struct page_node *data) {

	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct page_node *this;

	/* Figure out where to put new node */
	while (*new) {
		this = container_of(*new, struct page_node, rbnode);

		parent = *new;
		if (data->user_va == this->user_va) {
			return -1;
		}
		else if (data->user_va < this->user_va) {
			new = &((*new)->rb_left);
		}
		else {
			new = &((*new)->rb_right);
		}
	}

	/* Add new node and rebalance tree */
	rb_link_node(&data->rbnode, parent, new);
	rb_insert_color(&data->rbnode, root);

	return 0;
}

void rm_ht_init(void) {

	hash_init(rm_ht, RM_HT_BITS);
}

void ma_ht_init(void) {
	
	hash_init(ma_ht, MA_HT_BITS);
}

struct remote_map *rm_ht_get(void *user_va) {

	struct remote_map *rm;

	hash_for_each_possible(rm_ht, rm, hlist, user_va) {
		if (rm->user_va == user_va) {
			return rm;
		}
	}
	
	return NULL;
}

struct mmap_area *ma_ht_get(void *mmap_va) {
	
	struct mmap_area *ma;

	hash_for_each_possible(ma_ht, ma, hlist, mmap_va) {
		if (rm->mmap_va == mmap_va) {
			return ma;
		}
	}

	return NULL;
}

int rm_ht_add(void *user_va, unsigned int node, struct remote_page *rp) {

	struct remote_map *rm;

	rm = (struct remote_map *)kmalloc(sizeof(struct remote_map), GFP_KERNEL);
	if (rm == NULL)
		return -1;

	rm->user_va = user_va;
	rm->node = node;
	rm->rp = rp;

	hash_add(rm_ht, &(rm->hlist), user_va);

	return 0;
}

int rm_ht_del(void *user_va) {

	struct remote_map *rm;

	rm = rm_ht_get(user_va);
	if (rm == NULL)
		return -1;

	hash_del(&(rm->hlist));

	return 0;
}


int ma_ht_add_mmap(void *mmap_va, int npages, void* vma) {

	struct mmap_area *ma;

	ma = (struct mmap_area *)kmalloc(sizeof(struct mmap_area), GFP_KERNEL);
	if (ma == NULL)
			return -1;

	ma->mmap_va = mmap_va;
	ma->npages = npages;
	ma->vma = vma;
	INIT_LIST_HEAD(&(ma->list));
	ma->rbtree = RB_ROOT;

	hash_add(ma_ht, &(ma->hlist), mmap_va);

	return 0;
}	

int ma_ht_add_page(void *mmap_va, void *user_va, struct local_page *lp) {

	struct mmap_area *ma;
	struct page_node *pn;
	
	ma = ma_ht_get(mmap_va);
	if (ma == NULL)
		return -1;

	pn = (struct page_node *)kmalloc(sizeof(struct page_node), GFP_KERNEL);
	if (pn == NULL)
		return -2;

	pn->user_va = user_va;
	pn->lp = lp;
	list_add(&(pn->list), &(ma->list));
	if (pn_insert(&(ma->rbtree), pn) < 0) {
		list_del(&(pn->list));
		free(pn);
		return -3;
	}

	return 0;
}

int ma_ht_change_lp(void *mmap_va, void *user_va, struct local_page *lp) {

	struct mmap_area *ma;
	struct page_node *pn;
	
	ma = ma_ht_get(mmap_va);
	if (ma == NULL)
		return -1;

	pn = pn_search(&(ma->rbtree), user_va);
	if (pn == NULL)
		return -2;

	pn->lp = lp;
	
	return 0;
}

int ma_ht_del_mmap(void *mmap_va) {

	struct mmap_area *ma;
	struct list_head *lh, *temp_lh;
	struct page_node *pn, *temp_pn;
	//struct rb_node *rn, *temp_rn;
	
	ma = ma_ht_get(mmap_va);
	if (ma == NULL)
		return -1;

	// destroy list
	lh = ma->list;
	if (!list_empty(lh)) {
		for (lh = lh->next; lh != &(ma->list);) {
			temp_lh = lh;
			lh = lh->next;
			list_del_init(temp_lh);
		}
	}

	// destroy rbtree
	rbtree_postorder_for_each_entry_safe(pn, temp_pn, &(ma->rbtree), rbnode) {
		kfree(temp_pn);
	}

	/*
	for (rn = rb_first(&(ma->rbtree)); rn != NULL;) {
		temp_rn = rn;
		rn = rb_next(rn);
		rb_erase(temp_rn, &(ma->rbtree));
		kfree(container_of(temp_rn, struct page_node, rbnode));
	}
	*/
	
	// remove hash table entry
	hash_del(&(ma->hlist));
	kfree(ma);
}

void rm_ht_destroy(void) {

	struct mmap_area *ma, *temp_ma;
	int bkt;

	hash_for_each_safe(ma_ht, bkt, temp_ma, ma, hlist) {
		kfree(temp_ma);
	}
}

void ma_ht_destroy(void) {

	struct mmap_area *ma, *temp_ma;
	struct list_head *lh, *temp_lh;
	struct page_node *pn, *temp_pn;
	//struct rb_node *rn, *temp_rn;
	int bkt;

	hash_for_each_safe(ma_ht, bkt, temp_ma, ma, hlist) {
		// destroy list
		lh = ma->list;
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &(ma->list);) {
				temp_lh = lh;
				lh = lh->next;
				list_del_init(temp_lh);
			}
		}

		// destroy rbtree
		rbtree_postorder_for_each_entry_safe(pn, temp_pn, &(ma->rbtree), rbnode) {
			kfree(temp_pn);
		}
		
		/*
		for (rn = rb_first(&(ma->rbtree)); rn != NULL;) {
			temp_rn = rn;
			rn = rb_next(rn);
			rb_erase(temp_rn, &(ma->rbtree));
			kfree(container_of(temp_rn, struct page_node, rbnode));
		}
		*/

		// remove hash table entry
		hash_del(&(temp_ma->hlist));
		kfree(temp_ma);
	}
}

