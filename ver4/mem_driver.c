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
#include <linux/kthread.h>
#include <linux/delay.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>

#include "mem_config.h"
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
int memory_remove_pte(struct local_page *lp);
int memory_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Structure for file operations */
struct file_operations memory_fops = {
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
int node; // node number (0 ~ NNODES-1)
char* nodeip[NNODES];
int port;

struct semaphore sem;
struct task_struct *gt_id = NULL; // kthread

void* slabs[NSLABS];
int slabs_succ = -1;

struct list_head free_list[NNODES];

unsigned long max_pages = NPAGES * NNODES; // max # of mmap'able pages
unsigned long mmap_pages = 0; // # of current mmap'ed pages

/* Declaration of the init and exit functions */
module_init(memory_init);
module_exit(memory_exit);

/* Module parameter */
module_param(node, int, 0000);

int memory_init(void) {

	printk("[%s]\n", __FUNCTION__);
#if(DEBUG)
	printk("node: %d\n", node);
	printk("max mmap pages: %lu\n", max_pages);
#endif

	unsigned int i, j, k;
	int ret;
	struct list_head *lh, *temp_lh;

	// allocate memory for node IP addresses
	for (i = 0; i < NNODES; i++) {
		nodeip[i] = kmalloc(32, GFP_KERNEL);
		if (nodeip[i] == NULL) {
			printk("<error> nodeip kmalloc failure: nodeip[%d]\n", i);
			goto fail0;
		}
	}
	
	// read nodeip config file
	struct file *fp;
	char buf[32];
	int offset = 0;
	int node_i = 0;

	fp = filp_open(NODEIP_CONFIG_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		printk("<error> cannot open file %d\n", PTR_ERR(fp));
		goto fail1;
	}

	while (1) {
		ret = kernel_read(fp, offset, buf, 32);
		if (ret > 0) {
			if (node_i >= NNODES)
				break;
			/*
			for (i = 0; i < 32; i++) {
				printk("%c", buf[i]);
			}
			*/
			printk("%s", buf);
			printk("\n");
			//strcpy(nodeip[node_i], buf);
			//printk("%d: %s\n", node_i, nodeip[node_i]);
			offset += ret;
			node_i++;
		}
		else
			break;
	}

	filp_close(fp, NULL);

	// calculate port
	port = RDMA_PORT + node;

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

	// intialize structures & lists
	for (i = 0; i < NNODES; i++) {
		INIT_LIST_HEAD(&free_list[i]);
	}

	// local
	struct local_page *temp_lp;

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
			list_add(&(temp_lp->list), &free_list[node]);
		}
	}

	// remote
	struct remote_page *temp_rp;

	for (i = 0; i < NNODES; i++) {
		if (i != node) {
			for (j = 0; j < NSLABS; j++) {
				for (k = 0; k < NPAGES_SLAB; k++) {
					temp_rp = (struct remote_page *)kmalloc(sizeof(struct remote_page), GFP_KERNEL);
					if (temp_rp == NULL) {
						printk("<error> remote_page kmalloc failed\n");
						ret = -ENOMEM;
						goto fail2;
					}
					temp_rp->node = i;
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
	// kfree local free list
	if (!list_empty(&free_list[node])) {
		for (lh = free_list[node].next; lh != &free_list[node];) {
			temp_lh = lh;
			lh = lh->next;
			list_del(temp_lh);
			kfree(list_entry(temp_lh, struct local_page, list));
		}
	}

	// kfree remote free lists
	for (i = 0; i < NNODES; i++) {
		if (i != node) {
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
	// kfree nodeip
	for (i = 0; i < NNODES; i++) {
		kfree(nodeip[i]);
	}

fail0:
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

	// kfree nodeip
	for (i = 0; i < NNODES; i++) {
		kfree(nodeip[i]);
	}

	// kfree slabs
	for (i = 0; i < NSLABS; i++) {
		kfree(slabs[i]);
	}

	// kfree local free list
	struct list_head *lh, *temp_lh;
	if (!list_empty(&free_list[node])) {
		for (lh = free_list[node].next; lh != &free_list[node];) {
			temp_lh = lh;
			lh = lh->next;
			list_del(temp_lh);
			kfree(list_entry(temp_lh, struct local_page, list));
		}
	}
		
	// kfree remote free & alloc lists
	for (i = 0; i < NNODES; i++) {
		if (i != node) {
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

	return 0;
}

int memory_release(struct inode *inode, struct file *filp) {

	printk("[%s]\n", __FUNCTION__);

	// destroy hash tables
	rm_ht_destroy();
	ma_ht_destroy(&mmap_pages);

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
	struct mmap_area *ma;
	struct local_page *lp;
	struct remote_page *rp;

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
	else { // if exact multiple, add extra free page each
		npages += NNODES;
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
	ret = ma_ht_add(vma->vm_start, npages, (void*)vma);
	if (ret < 0) {
		printk("<error> ma_ht_add failure: %d\n", ret);
		return ret;
	}
	ma = ma_ht_get(vma->vm_start);
	if (ma == NULL) {
		printk("<error> ma_ht_get failure\n");
		return -1;
	}

	// find & link local free pages
	npages_t = npages / NNODES;

	while (npages_t > 0) {
		lh = &free_list[node];
		if (list_empty(lh)) {
			printk("<error> local free list empty during mmap\n");
			return -ENOMEM;
		}
		lh = lh->prev;
		lp = list_entry(lh, struct local_page, list);
		lp->user_va = start;
		lp->vma = vma;
		pfn = page_to_pfn(virt_to_page(lp->slab_va));
		if ((ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE,
						PAGE_SHARED)) < 0) {
			return ret;
		}

		// move to local_alloc_list
		list_del_init(lh);
		list_add(lh, &(ma->local_alloc_list));

		start += PAGE_SIZE;
		npages_t -= 1;
	}

	// find remote free pages
	for (i = 0; i < NNODES; i++) {
		if (i != node) {
			npages_t = npages / NNODES;
		
			while (npages_t > 0) {
				lh = &free_list[i];
				if (list_empty(lh)) {
					printk("<error> remote free list empty during mmap\n");
					return -ENOMEM;
				}
				lh = lh->prev;
				rp = list_entry(lh, struct remote_page, list);
		
				// move to remote_free_list
				list_del_init(lh);
				list_add(lh, &(ma->remote_free_list));

				npages_t -= 1;
			}
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

	//down(&sem);

	/* Local page eviction */
	struct mmap_area *ma;
	struct list_head *lh;
	struct local_page *lp;
	struct remote_page *rp;

	ma = ma_ht_get(mmap_va);
	if (ma == NULL) {
		printk("<error> ma_ht_get failure\n");
		ret = VM_FAULT_ERROR;
		goto out;
	}

	// reinsert to alloc list
	lh = &(ma->local_alloc_list);
	if (list_empty(lh)) {
		printk("<error> local_alloc_list empty\n");
		ret = VM_FAULT_ERROR;
		goto out;
	}
	lh = lh->prev;
	lp = list_entry(lh, struct local_page, list);
	list_del_init(lh);
	list_add(lh, &(ma->local_alloc_list));
	
	// find free remote page & move to alloc list
	lh = &(ma->remote_free_list);
	if (list_empty(lh)) {
		printk("<error> remote_free_list empty\n");
		ret = VM_FAULT_ERROR;
		goto out;
	}
	lh = lh->prev;
	rp = list_entry(lh, struct remote_page, list);
	list_del_init(lh);
	list_add(lh, &(ma->remote_alloc_list));
	
	// RDMA write content
#if(USE_RDMA)
	ret = host_rdma_write(lp->slab_no, lp->slab_pgoff, rp->node, rp->slab_no, rp->slab_pgoff);
	if (ret < 0) {
		printk("<error> host_rdma_write failure: %d\n", ret);
		ret = VM_FAULT_ERROR;
		goto out;
	}
#endif

	// save remote page info
	ret = rm_ht_add(lp->user_va, rp);
	if (ret < 0) {
		printk("<error> rm_ht_add failure: %d\n", ret);
		ret = VM_FAULT_ERROR;
		goto out;
	}

	/* Check for previous eviction */
	struct remote_map *rm = rm_ht_get(user_va);

	if (rm != NULL) {
		rp = rm->rp;
		lh = &(rp->list);

#if(USE_RDMA)
		ret = host_rdma_read(lp->slab_no, lp->slab_pgoff, rp->node, rp->slab_no, rp->slab_pgoff);
		if (ret < 0) {
			printk("<error> host_rdma_read failure: %d\n", ret);
			ret = VM_FAULT_ERROR;
			goto out;
		}
#endif

		list_del_init(lh);
		list_add(lh, &(ma->remote_free_list));

		rm_ht_del(user_va);
	}

	/* Remove PTE */
	ret = memory_remove_pte(lp);
	if (ret < 0) {
		printk("<error> memory_remove_pte failure: %d\n", ret);
		ret = VM_FAULT_ERROR;
		goto out;
	}

	/* Link local page to va */
	struct page *pp;

	lp->user_va = user_va;
	lp->vma = vma;
	pp = virt_to_page(lp->slab_va);
	vmf->page = pp;
	get_page(vmf->page);

	//up(&sem);
	return 0;

out:
	//up(&sem);
	return ret;
}

int memory_remove_pte(struct local_page *lp) {

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
		return -1;
	}

	pud = pud_offset(pgd, del_user_va);
	if (pud_none(*pud) || pud_bad(*pud)) {
		printk("<error> invalid pud\n");
		return -1;
	}

	pmd = pmd_offset(pud, del_user_va);
	if (pmd_none(*pmd) || pmd_bad(*pmd)) {
		printk("<error> invalid pmd\n");
		return -1;
	}

	ptep = pte_offset_kernel(pmd, del_user_va);
	if (!ptep) {
		printk("<error> invalid pte\n");
		return -1;
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

	return 0;
}

int memory_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

	printk("[%s]\n", __FUNCTION__);
	
	int ret = 0;
	struct mmap_area *ma;
	struct munmap_info info;

	switch(cmd) {
		case 7:
		/* munmap */
		copy_from_user(&info, (const void *)arg, sizeof(struct munmap_info));

#if(DEBUG)
		printk("ioctl munmap\n");
		printk("addr: %p, size: %lu\n", info.addr, info.length);
#endif

		ma = ma_ht_get(info.addr);
		if (ma == NULL) {
			printk("<error> ma_ht_get failure\n");
			return -EINVAL;
		}

		mmap_pages -= ma->npages;

		ret = ma_ht_del(info.addr);
		if (ret < 0) {
			printk("<error> ma_ht_del failure: %d\n", ret);
			return ret;
		}
		
		break;
	default:
#if(DEBUG)
		printk("ioctl default\n");
#endif
		break;
	}

	return 0;
}
