#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>

#include "mem_driver.h"
#include "mem_ds.h"
#include "mem_ioctl.h"
#if(USE_RDMA)
#include "mem_rdma.h"
#endif

MODULE_LICENSE("Dual BSD/GPL");

/* Function declarations */
int memory_open(struct inode *inode, struct file *filp);
int memory_release(struct inode *inode, struct file *filp);
ssize_t memory_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t memory_write(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t memory_mmap(struct file *flip, struct vm_area_struct *vma);
void memory_vma_open(struct vm_area_struct *vma);
void memory_vma_close(struct vm_area_struct *vma);
int memory_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int memory_init(void);
void memory_exit(void);
void memory_remove_pte(struct local_page *lp);
int memory_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Structure for file operations */
struct file_operations memory_fops = {
		read: memory_read,
		write: memory_write,
		open: memory_open,
		release: memory_release,
		mmap: memory_mmap,
		unlocked_ioctl: memory_ioctl,
};

/* Structure for vm operations */
struct vm_operations_struct memory_vm_ops = {
		open: memory_vma_open,
		close: memory_vma_close,
		fault: memory_fault,
};


/* Global variables */
static char *type; // module parameter (local/remote)
int is_local = -1;

struct semaphore sem;
struct task_struct *gt_id = NULL; // kthread
extern struct memory_cb *gcb; // RDMA control block

void* slabs[NSLABS];
int slabs_succ = -1;

struct local_page* avail_lp[NSLABS][NPAGES_SLAB]; // only for local node TODO

struct list_head free_list[NNODES];
struct list_head alloc_list[NNODES];

unsigned long max_pages = NPAGES * NNODES; // max # of mmap'able pages
unsigned long mmap_pages = 0; // # of current mmap'ed pages

/* Declaration of the init and exit functions */
module_init(memory_init);
module_exit(memory_exit);

/* Module parameter */
module_param(type, charp, 0000);

int memory_init(void) {

	printk("[%s]\n", __FUNCTION__);
#if(DEBUG)
	printk("type: %s\n", type);
	printk("max mmap pages: %lu\n", max_pages);
#endif

	unsigned int i, j, k;
	int ret;

	// check type
	if (strcmp(type, "local") == 0)
		is_local = 1;
	else if (strcmp(type, "remote") == 0)
		is_local = 0;
	else {
		printk("<error> invalid type: %s\n", type);
		ret = -EINVAL;
		goto fail1;
	}

	// register device
	ret = register_chrdev(MEMORY_MAJOR, "memory", &memory_fops);
	if (ret < 0) {
		printk("<error> cannot obtain major number %d\n", MEMORY_MAJOR);
		goto fail1;
	}

	// initialize semaphore
	sema_init(&sem, 1);

	// kmalloc slabs
	for (i = 0; i < NSLABS; i++) {
		slabs[i] = kmalloc(NPAGES_SLAB * PAGE_SIZE, GFP_KERNEL);
		if (slabs[i] == NULL) {
			printk("<error> slab kmalloc failed: slabs[%d]\n", i);
			ret = -ENOMEM;
			goto fail2;
		}
		slabs_succ = i;
	}

	// intialize structures & lists TODO: too much memory?
	if (is_local) {
		for (i = 0; i < NNODES; i++) {
			INIT_LIST_HEAD(&free_list[i]);
			INIT_LIST_HEAD(&alloc_list[i]);
		}

		// local
		struct local_page *temp_lp;
		struct lp_node *temp_lpn;

		for (i = 0; i < NSLABS; i++) {
			for (j = 0; j < NPAGES_SLAB; j++) {
				temp_lp = (struct local_page *)kmalloc(sizeof(struct local_page), GFP_KERNEL);
				if (temp_lp == NULL) {
					printk("<error> local_page kmalloc failed\n");
					ret = -ENOMEM;
					goto fail2;
				}
				temp_lp->user_va = 0;
				temp_lp->slab_no = i;
				temp_lp->slab_pgoff = j;
				temp_lp->slab_va = slabs[i] + PAGE_SIZE * j;
				temp_lp->vma = NULL;
				avail_lp[i][j] = temp_lp;
	
				// add to free list
				temp_lpn = (struct lp_node *)kmalloc(sizeof(struct lp_node), GFP_KERNEL);
				if (temp_lpn == NULL) {
					printk("<error> lp_node kmalloc failed\n");
					ret = -ENOMEM;
					goto fail2;
				}
				temp_lpn->lp = temp_lp;
				list_add(&(temp_lpn->list), &free_list[0]);
			}
		}

		// remote
		struct remote_page *temp_rp;

		for (i = 1; i < NNODES; i++) {
			for (j = 0; j < NSLABS; j++) {
				for (k = 0; k < NPAGES_SLAB; k++) {
					temp_rp = (struct remote_page *)kmalloc(sizeof(struct remote_page), GFP_KERNEL);
					if (temp_rp == NULL) {
						printk("<error> remote_page kmalloc failed\n");
						ret = -ENOMEM;
						goto fail2;
					}
					temp_rp->slab_no = j;
					temp_rp->slab_pgoff = k;
	
					// add to free list
					list_add(&(temp_rp->list), &free_list[i]);
				}
			}
		}
	}

#if(USE_RDMA)
	// RDMA setup
	ret = memory_rdma_init();
	if (ret < 0) {
		goto fail3;
	}
#endif

#if(DEBUG)
	printk("insmod success\n"); 
#endif
	return 0;

fail3:
	if (is_local) {
		// kfree avail_lp
		for (i = 0; i < NSLABS; i++) {
			for (j = 0; j < NPAGES_SLAB; j++) {
				kfree(avail_lp[i][j]);
			}
		}
		
		// kfree local free list
		struct list_head *lh, *temp_lh;
		if (!list_empty(&free_list[0])) {
			for (lh = free_list[0].next; lh != &free_list[0];) {
				temp_lh = lh;
				lh = lh->next;
				list_del(temp_lh);
				kfree(list_entry(temp_lh, struct lp_node, list));
			}
		}

		// kfree remote free lists
		struct list_head *lh, *temp_lh;
		for (i = 1; i < NNODES; i++) {
			if (!list_empty(&free_list[i])) {
				for (lh = free_list[i].next; lh != &free_list[i];) {
					temp_lh = lh;
					lh = lh->next;
					list_del(temp_lh);
					kfree(list_entry(temp_lh, struct remote_page, list));
				}
			}
		}
	}

fail2:
	// kfree slabs
	for (i = 0; i <= slabs_succ; i++) {
		kfree(slabs[i]);
	}
	
	// unregister device
	unregister_chrdev(MEMORY_MAJOR, "memory");

fail1:
	printk("<error> aborting insmod: %d\n", ret);
	return ret;
}

void memory_exit(void) {

	printk("[%s]\n", __FUNCTION__);

	unsigned int i, j;

#if(USE_RDMA)
	// finish RDMA
	memory_rdma_exit();
#endif

	// kfree slabs
	for (i = 0; i < NSLABS; i++) {
		kfree(slabs[i]);
	}

	if (is_local) {
		// kfree avail_lp
		for (i = 0; i < NSLABS; i++) {
			for (j = 0; j < NPAGES_SLAB; j++) {
				kfree(avail_lp[i][j]);
			}
		}

		// kfree local free & alloc lists
		struct list_head *lh, *temp_lh;
		if (!list_empty(&free_list[0])) {
			for (lh = free_list[0].next; lh != &free_list[0];) {
				temp_lh = lh;
				lh = lh->next;
				list_del(temp_lh);
				kfree(list_entry(temp_lh, struct lp_node, list));
			}
		}
		if (!list_empty(&alloc_list[0])) {
			for (lh = alloc_list[0].next; lh != &alloc_list[0];) {
				temp_lh = lh;
				lh = lh->next;
				list_del(temp_lh);
				kfree(list_entry(temp_lh, struct lp_node, list));
			}
		}

		// kfree remote free & alloc lists
		for (i = 1; i < NNODES; i++) {
			if (!list_empty(&free_list[i])) {
				for (lh = free_list[i].next; lh != &free_list[i];) {
					temp_lh = lh;
					lh = lh->next;
					list_del(temp_lh);
					kfree(list_entry(temp_lh, struct remote_page, list));
				}
			}
			if (!list_empty(&alloc_list[i])) {
				for (lh = alloc_list[i].next; lh != &alloc_list[i];) {
					temp_lh = lh;
					lh = lh->next;
					list_del(temp_lh);
					kfree(list_entry(temp_lh, struct remote_page, list));
				}
			}
		}
	}

	// unregister device
	unregister_chrdev(MEMORY_MAJOR, "memory");

#if(DEBUG)
	printk("rmmod success\n");
#endif
}

int memory_open(struct inode *inode, struct file *filp) {

	printk("[%s]\n", __FUNCTION__);

	// initialize hash tables
	rm_ht_init();
	ma_ht_init();

	// initialize mmap'ed size
	mmap_pages = 0;
	
	return 0;
}

int memory_release(struct inode *inode, struct file *filp) {

	printk("[%s]\n", __FUNCTION);

	unsigned int i;

	// destroy hash tables
	rm_ht_destroy();
	ma_ht_destroy();

	// move alloc_list entries to free_list
	for (i = 0; i < NNODES; i++) {
		struct list_head *lh = &alloc_list[i], *temp_lh;
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &alloc_list[i];) {
				temp_lh = lh;
				lh = lh->next;
				list_del_init(temp_lh);
				list_add(temp_lh, &free_list[i]);
			}
		}
	}

	// munmap all areas TODO

	return 0;
}

ssize_t memory_read(struct file *filp, char *buf, 
		size_t count, loff_t *f_pos) { 

	printk("[%s]\n", __FUNCTION__);

	return 0;
}

ssize_t memory_write( struct file *filp, char *buf,
		size_t count, loff_t *f_pos) {

	printk("[%s]\n", __FUNCTION__);

	return 0;
}

ssize_t memory_mmap(struct file *flip, struct vm_area_struct *vma) {

	printk("[%s]\n", __FUNCTION__);
#if(DEBUG)
	printk("vma: %p\n", vma);
#endif

	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long npages = size / PAGE_SIZE;
	unsigned long leftover;
	unsigned long npages_t;
	unsigned long pfn;
	int ret;
	unsigned int i;
	struct list_head *lh;
	struct lp_node *lpn;
	struct local_page *lp;

	// check pgoff
	if (vma->vm_pgoff != 0) {
		printk("<error> vma->vm_pgoff != 0\n");
		return -EIO;
	}

#if(DEBUG)
	printk("vm: %p-%p\n", vma->vm_start, vma->vm_end);
	printk("requested mmap size: %lu bytes, %lu pages\n", size, npages);
#endif

	// pad # of pages
	if ((leftover = npages % NNODES) != 0) {
		npages += (NNODES - leftover);
	}
#if(DEBUG)
	printk("actual mmap size: %lu pages\n", npages);
#endif

	// check mmap limit
	if (mmap_pages + npages > max_pages) {
		printk("<error> mmap limit exceeded\n");
		return -ENOMEM;
	}
	mmap_pages += npages;
#if(DEBUG)
	printk("# of pages left: %lu\n", max_pages - mmap_pages);
#endif
	
	// add hash table entry
	ma_ht_add_mmap(vma->vm_start, npages, (void*)vma);

	// find & link local free pages
	npages_t = npages / NNODES;
	lh = &free_list[0];

	while (npages_t > 0) {
		if (list_empty(&free_list[0])) {
			printk("<error> local free list empty during mmap\n");
			return -ENOMEM;
		}
		lh = lh->prev;
		lpn = list_entry(lh, struct lp_node, list);
		lp = lpn->lp;
		pfn = virt_to_pfn(lp->slab_va);
		if ((ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE,
						PAGE_SHARED)) < 0) {
			return ret;
		}
		
		ma_ht_add_page((void*)vma->vm_start, (void*)start, lp);

		start += PAGE_SIZE;
		npages_t -= 1;
	}

	// find remote free pages & save their info
	unsigned int slab_no;
	unsigned int slab_pgoff;
	struct remote_page *rp;
	
	for (i = 1; i < NNODES; i++) {
		npages_t = npages / NNODES;
		lh = &free_list[i];
		while (npages_t > 0) {
			if (list_empty(&free_list[i])) {
				printk("<error> remote free list empty during mmap\n");
				return -ENOMEM;
			}
			lh = lh->prev;
			rp = list_entry(lh, struct remote_node, list);
			
			ma_ht_add_page((void*)vma->vm_start, (void*)start, NULL);
			rm_ht_add((void*)start, i, rp);

			start += PAGE_SIZE;
			npages_t -= 1;
		}
	}

	// vma setup
	vma->vm_ops = &memory_vm_ops;
	vma->vm_flags |= VM_IO;
	memory_vma_open(vma);

	return 0;
}

void memory_vma_open(struct vm_area_struct *vma) {

#if(DEBUG)
	printk("[%s]\n", __FUNCTION__);
#endif
}

void memory_vma_close(struct vm_area_struct *vma) {

#if(DEBUG)
	printk("[%s]\n", __FUNCTION__);
#endif
}

int memory_fault(struct vm_area_struct *vma, struct vm_fault *vmf) {

	int ret = 0;
	int evicted = 0;
	unsigned long user_va = vmf->virtual_address;
	unsigned long mmap_va = vma->vm_start;
	
#if(DEBUG)
	printk("[%s]\n", __FUNCTION__);
	printk("user_va: %p\n", user_va);
	printk("vma: %p\n", vma);
	printk("mmap_va: %p\n", mmap_va);
#endif

	down(&sem);

	// select & evict page
	struct mmap_area *ma;
	struct list_head *lh;
	struct local_page *lp;
	struct page *pp;

	ma = ma_ht_get(mmap_va);
	if (ma == NULL) {
		printk("<error> ma_ht_get failure\n");
		ret = VM_FAULT_OOM;
		goto out;
	}

	lh = &(ma->list);
	lh = lh->prev;
	lp = list_entry(lh, struct local_page, list);
	lp->user_va = user_va;
	lp->vma = vma;
	pp = virt_to_page(lp->slab_va);
	vmf->page = pp;
	get_page(vmf->page);
	list_del_init(lh);
	list_add(lh, &(ma->list));

	// RDMA write TODO


	struct page *pp;
	struct local_page *local_lp;
	
	if (!list_empty(&free_list)) { // free page exists
#if(DEBUG)
		printk("free page exists\n");
#endif
		// get free page and link
		struct list_head *free_lh = free_list.prev;
		struct local_page *free_lp = list_entry(free_lh, struct local_page, list);
		local_lp = free_lp;

		free_lp->user_va = user_va;
		free_lp->vma = vma;
		pp = virt_to_page(free_lp->slab_va);
		vmf->page = pp;
		get_page(vmf->page);

		// move to alloc_list
		list_del_init(free_lh);
		list_add(free_lh, &alloc_list);

#if(DEBUG)
		print_lists();
#endif
	}
	else { // no free page, evict
		// get to-be evicted page (FIFO)
#if(DEBUG)
		printk("no free page, evict\n");
#endif
		unsigned int remote_slab_no, remote_pgoff;
		struct list_head *evict_lh = alloc_list.prev;
		struct local_page *evict_lp = list_entry(evict_lh, struct local_page, list);
		local_lp = evict_lp;

		// RDMA write that page to remote node
#if(USE_RDMA)
		ret = server_ask_free(&remote_slab_no, &remote_pgoff);
		if (ret < 0) {
			printk("<error> server has no free page\n");
			ret = VM_FAULT_OOM;
			goto out;
		}
		ret = server_rdma_write(evict_lp->slab_no, evict_lp->slab_pgoff, \
				remote_slab_no, remote_pgoff);
		if (ret < 0) {
			printk("<error> RDMA write failure: %d\n", ret);
			goto out;
		}
#endif

		// save page info in rm_ht
		// TODO: need to change node
		if (rm_ht_put(1, evict_lp->user_va, 2, remote_slab_no, remote_pgoff, evict_lp) < 0) {
			printk("<error> out of memory during rm_ht_put\n");
			ret = VM_FAULT_OOM;
			goto out;
		}

		// remove page table entry of evicted page's user va
		memory_remove_pte(evict_lp);

		// link page
		evict_lp->user_va = user_va;
		evict_lp->vma = vma;
		pp = virt_to_page(evict_lp->slab_va);
		vmf->page = pp;
		get_page(vmf->page);

		// move to front of alloc list
		list_move(evict_lh, &alloc_list);
	}

#if(DEBUG)
	printk("local_lp: %p, slab_no: %u, slab_pgoff: %u\n", local_lp, local_lp->slab_no, local_lp->slab_pgoff);
#endif

	/* Check eviction record */
	struct remote_map *remote_rm = rm_ht_get(user_va);

	if (remote_rm == NULL || (remote_rm != NULL && !(remote_rm->valid))) { // no record
		// add the page to mmap structure
		ret = mp_ht_add_page(mmap_va, user_va, local_lp);
		if (ret < 0) {
			printk("<error> mp_ht_add_page failure: %d\n", ret);
			goto out;
		}
	}
	else { // previously evicted
		// change local_page pointer
		ret = mp_ht_change_lp(mmap_va, user_va, local_lp);
		if (ret < 0) {
			printk("<error> mp_ht_change_lp failure: %d\n", ret);
			goto out;
		}

		// RDMA read
#if(USE_RDMA)
		ret = server_rdma_read(local_lp->slab_no, local_lp->slab_pgoff, \
				remote_rm->slab_no, remote_rm->slab_pgoff);
		if (ret < 0) {
			printk("<error> server_rdma_read failure: %d\n", ret);
			goto out;
		}

		// tell remote to free
		ret = server_tell_free(0, &remote_rm->slab_no, &remote_rm->slab_pgoff, 1);
		if (ret < 0) {
			printk("<error> server_tell_free failure: %d\n", ret);
			goto out;
		}
#endif

		// invalidate eviction record
		ret = rm_ht_put(0, user_va, 0, 0, 0, 0);
		if (ret < 0) {
			printk("<error> rm_ht_put failure: %d\n", ret);
			goto out;
		}
	}

	up(&sem);
	return 0;

out:
	up(&sem);
	return ret;
}

void memory_remove_pte(struct local_page *lp) {

	unsigned long del_user_va = lp->user_va;
	unsigned long del_slab_va = lp->slab_va;
	unsigned long del_pfn = page_to_pfn(virt_to_page(del_slab_va));
	struct vm_area_struct *del_vma = lp->vma;
	struct mm_struct *del_mm = del_vma->vm_mm;

#if(DEBUG)
	printk("[%s]\n", __FUNCTION__);
	printk("del_user_va: %p\n", del_user_va);
	printk("del_slab_va: %p\n", del_slab_va);
	printk("del_pfn: %p\n", del_pfn);
	printk("del_vma: %p\n", del_vma);
	printk("del_mm: %p\n", del_mm);
#endif

	// TODO: find PTE (need to be changed for x86)
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	pgd = pgd_offset(del_mm, del_user_va);
	if (pgd_none(*pgd) || pgd_bad(*pgd)) {
		printk("<error> invalid pgd\n");
		return;
	}

	pud = pud_offset(pgd, del_user_va);
	if (pud_none(*pud) || pud_bad(*pud)) {
		printk("<error> invalid pud\n");
		return;
	}

	pmd = pmd_offset(pud, del_user_va);
	if (pmd_none(*pmd) || pmd_bad(*pmd)) {
		printk("<error> invalid pmd\n");
		return;
	}

	ptep = pte_offset_kernel(pmd, del_user_va);
	if (!ptep) {
		printk("<error> invalid pte\n");
		return;
	}

#if(DEBUG)
	printk("ptep: %p\n", ptep);
	printk("pte: %p\n", *ptep);
	printk("pfn: %p\n", pte_pfn(*ptep));
#endif

	// flush cache
	flush_cache_page(del_vma, del_user_va, del_pfn);

	// clear PTE
	pte_clear(del_mm, del_user_va, ptep);

	// flush TLB
	flush_tlb_page(del_vma, del_user_va);
}

int memory_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

	printk("[%s]\n", __FUNCTION__);
	
	int ret = 0;
	int i;
	struct remote_map *rm;
	struct local_page *lp;
	struct mmap_page *mp;
	struct page_node *pn;
	struct list_head *lh;
	struct list_head *temp;
	unsigned int *slabs;
	unsigned int *pgoffs;
	unsigned int cnt = 0;
	unsigned int nchunks = 0;
	unsigned int *nitems;
	unsigned int leftover = 0;
	struct munmap_info info;

	switch(cmd) {
		case 7:
		/* munmap */
		copy_from_user(&info, (const void *)arg, sizeof(struct munmap_info));

#if(DEBUG)
		printk("ioctl munmap\n");
		printk("addr: %p, size: %lu\n", info.addr, info.length);
#endif

		mp = mp_ht_get(info.addr);
		if (mp == NULL) {
			printk("<error> trying to munmap invalid address\n");
			ret = -EINVAL;
			goto out;
		}

		lh = &(mp->list);

		// traverse list and check if remote
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &(mp->list); lh = lh->next) {
				pn = list_entry(lh, struct page_node, list);

				rm = rm_ht_get(pn->user_va);
				if ((rm != NULL) && (rm->valid == 1)) { // remote, increment count
					cnt++;
				}
				else { // local, move page to free list
					lp = pn->lp;
#if(DEBUG)
					printk("freeing slab: %u, pgoff: %u\n", lp->slab_no, lp->slab_pgoff);
#endif
					temp = &(lp->list);
					list_del_init(temp);
					list_add(temp, &free_list);
				}
			}
		}

#if(DEBUG)
		printk("# of remote pages: %u\n", cnt);
#endif

		if (cnt != 0) { // there are remote pages
			// calculate # of chunks & # of items in each chunk
			leftover = cnt % CHUNK_SIZE;
			if (leftover != 0) {
				nchunks = (cnt - leftover) / CHUNK_SIZE + 1;
			}
			else {
				nchunks = cnt / CHUNK_SIZE;
			}
			nitems = (unsigned int *)kmalloc(sizeof(unsigned int) * nchunks, GFP_KERNEL);
			if (nitems == NULL) {
				printk("<error> out of memory during ioctl\n");
				ret = -ENOMEM;
				goto out;
			}
			for (i = 0; i < nchunks; i++) {
				nitems[i] = CHUNK_SIZE;
			}
			if (leftover != 0) {
				nitems[nchunks-1] = leftover;
			}
		
			// allocate arrays
			slabs = (unsigned int *)kmalloc(sizeof(unsigned int) * CHUNK_SIZE, GFP_KERNEL);
			if (slabs == NULL) {
				printk("<error> out of memory during ioctl\n");
				ret = -ENOMEM;
				goto out;
			}
			pgoffs = (unsigned int *)kmalloc(sizeof(unsigned int) * CHUNK_SIZE, GFP_KERNEL);
			if (pgoffs == NULL) {
				printk("<error> out of memory during ioctl\n");
				ret = -ENOMEM;
				goto out;
			}

			// construct & send munmap arrays
			lh = &(mp->list);
			i = 0;
			unsigned int chunk_i = 0;
			unsigned int t_cnt = cnt;
			for (lh = lh->next; lh != &(mp->list); lh = lh->next) {
				pn = list_entry(lh, struct page_node, list);
				rm = rm_ht_get(pn->user_va);
				if ((rm != NULL) && (rm->valid == 1)) {
					slabs[i] = rm->slab_no;
					pgoffs[i] = rm->slab_pgoff;
					i++;
					t_cnt--;
					if (i >= CHUNK_SIZE || t_cnt == 0) {
						i = 0;
#if(USE_RDMA)
						ret = server_tell_free(info.addr, slabs, pgoffs, nitems[chunk_i]);
						if (ret < 0) {
							printk("<error> server_tell_free failure: %d\n", ret);
							goto out;
						}
#endif
						chunk_i++;
					}
				}
			}

			kfree(slabs);
			kfree(pgoffs);
		}

		// reduce mmap page count
		mmap_pages -= mp->npages;
		
		break;
	default:
#if(DEBUG)
		printk("ioctl default\n");
#endif
		break;
	}

out:
	return ret;
}
