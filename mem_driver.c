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

#define USE_RDMA 1 // Set to use RDMA

#if(USE_RDMA)
#include "mem_rdma.h"
#endif
#include "mem_driver.h"
#include "mem_hash.h"
#include "mem_ioctl.h"

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
void memory_remove_pte(struct list_node *ln);
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

/* List node structure */
struct list_node {
	struct list_head list;
	unsigned long user_va;
	int slab_no; // local slab number
	int slab_pgoff; // local slab page offset
	unsigned long slab_va;
	struct vm_area_struct *vma;
};

/* Global variables */
static char *cmd; // module parameter (server/client,IP,port)
struct semaphore sem;
struct task_struct *gt_id = NULL; // kthread
extern struct memory_cb *gcb; // RDMA control block
struct mm_struct *i_mm; // init_mm
void* slabs[NSLABS];
int slabs_succ = -1;
char avail[NSLABS][NPAGES_SLAB];
// TODO: keep free_list & alloc_list only for local node
struct list_head free_list;
struct list_head alloc_list;
int free_cnt = NPAGES;
int alloc_cnt = 0; // # of allocated pages

/* Declaration of the init and exit functions */
module_init(memory_init);
module_exit(memory_exit);

/* Module parameter */
module_param(cmd, charp, 0000);

void list_destroy(void) {

	struct list_head *temp;

	// ensure alloc_list is not empty
	if (list_empty(&alloc_list)) {
		printk("alloc_list empty\n");
		goto free;
	}

	// free nodes
	for (temp = alloc_list.next; temp != &alloc_list;) {
		struct list_node *ln = list_entry(temp, struct list_node, list);
		temp = temp->next;
		kfree(ln);
	}

free:
	// ensure free_list is not empty
	if (list_empty(&free_list)) {
		printk("free_list empty\n");
		return;
	}

	// free nodes
	for (temp = free_list.next; temp != &free_list;) {
		struct list_node *ln = list_entry(temp, struct list_node, list);
		temp = temp->next;
		kfree(ln);
	}
}

#if(USE_RDMA)
/*
static int kthread_rdma(void *arg) {

	int rc;

	printk("%s started\n", __FUNCTION__);

	// begin RDMA
	rc = memory_rdma_exec(cmd);

	return 0;
}
*/
#endif

int memory_init(void) {

	printk("memory_init\n");

	int i, j, result;

	printk("cmd: %s\n", cmd);

	/* init_mm */
	i_mm = init_task.active_mm;

	/* Registering device */
	result = register_chrdev(MEMORY_MAJOR, "memory", &memory_fops);
	if (result < 0) {
		printk("<1>memory: cannot obtain major number %d\n", MEMORY_MAJOR);
		return result;
	}

	/* Initialize semaphore */
	sema_init(&sem, 1);

	/* kmalloc slabs */
	for (i = 0; i < NSLABS; i++) {
		slabs[i] = kmalloc(NPAGES_SLAB * PAGE_SIZE, GFP_KERNEL);
		if (slabs[i] == NULL) {
			printk("slab kmalloc failed: slabs[%d]\n", i);
			result = -ENOMEM;
			goto fail;
		}
		slabs_succ = i;
	}

	/* initialize avail array */
	for (i = 0; i < NSLABS; i++) {
		for (j = 0; j < NPAGES_SLAB; j++) {
			avail[i][j] = 'f';
		}
	}

#if(USE_RDMA)
	/* Create & run RDMA daemon(s) */
	//gt_id = (struct task_struct *)kthread_run(kthread_rdma, NULL, "kthread_rdma");

	/* Setup RDMA */
	result = memory_rdma_init(cmd);
	if (result) {
		goto fail;
	}
#endif

	printk("<1>Inserting memory module\n"); 
	return 0;

fail:
	printk("Aborting insmod...\n");
	for (i = 0; i <= slabs_succ; i++) {
		kfree(slabs[i]);
	}
	unregister_chrdev(MEMORY_MAJOR, "memory");
	return result;
}

void memory_exit(void) {

	int i;

#if(USE_RDMA)
	/* Finish RDMA */
	memory_rdma_exit();
#endif

	/* kfree slabs */
	for (i = 0; i < NSLABS; i++) {
		kfree(slabs[i]);
	}

	/* Freeing the major number */
	unregister_chrdev(MEMORY_MAJOR, "memory");

	printk("<1>Removing memory module\n");
}

int memory_open(struct inode *inode, struct file *filp) {

	printk("memory open\n");

	int result;
	unsigned long i, j;
	unsigned long temp_succ = -1;

	/* Initialize hash tables */
	rm_ht_init();
	mp_ht_init();

	/* Initialize lists */
	INIT_LIST_HEAD(&alloc_list);
	INIT_LIST_HEAD(&free_list);

	/* Insert slabs' pages into free list */
	for (i = 0; i < NSLABS; i++) {
		for (j = 0; j < NPAGES_SLAB; j++) {
			struct list_node *temp = (struct list_node *) kmalloc(sizeof(struct list_node), GFP_KERNEL);
			if (temp == NULL) {
				printk("out of memory\n");
				result = -ENOMEM;
				goto fail;
			}
			temp->user_va = 0;
			temp->slab_no = i;
			temp->slab_pgoff = j;
			temp->slab_va = (unsigned long)slabs[i] + (unsigned long)(PAGE_SIZE * j);
			temp->vma = NULL;	
			list_add(&(temp->list), &free_list);
		}
	}

	/* Set counts */
	free_cnt = NPAGES;
	alloc_cnt = 0;

	return 0;

fail:
	return result;
}

int memory_release(struct inode *inode, struct file *filp) {

	printk("memory release\n");

	/* Destroy lists */
	list_destroy();

	/* Destroy hash tables */
	rm_ht_destroy();
	//mp_ht_destroy();

	return 0;
}

ssize_t memory_read(struct file *filp, char *buf, 
		size_t count, loff_t *f_pos) { 

	printk("memory read\n");
	return 0;
}

ssize_t memory_write( struct file *filp, char *buf,
		size_t count, loff_t *f_pos) {

	printk("memory write\n");
	return 0;
}

ssize_t memory_mmap(struct file *flip, struct vm_area_struct *vma)
{
	static unsigned long mmap_cnt = 0;

	printk("memory_mmap begin\n");

	if (vma->vm_pgoff != 0) {
		printk("vma->vm_pgoff != 0\n");
		return -EIO;
	}

	printk("vma->vm_start = %p\n", vma->vm_start);
	printk("vma->vm_end = %p\n", vma->vm_end);
	unsigned long mmap_size = vma->vm_end - vma->vm_start;
	printk("mmap size: %p\n", mmap_size);

	vma->vm_ops = &memory_vm_ops;
	vma->vm_flags |= VM_IO;
	memory_vma_open(vma);

#if(USE_RDMA)
	server_rdma_read(0, 0, 0, 0);
#endif

	mmap_cnt++;

	printk("memory_mmap end\n");
	return 0;
}

void memory_vma_open(struct vm_area_struct *vma) {
	printk("memory_vma_open\n");
}

void memory_vma_close(struct vm_area_struct *vma) {
	printk("memory_vma_close\n");
}

int memory_fault(struct vm_area_struct *vma, struct vm_fault *vmf) {

	int ret;
	
	down(&sem);

	unsigned long user_va = vmf->virtual_address;
	printk("fault!!! at %p\n", user_va);

	/* Allocate a page for the faulted page */
	struct page *pp;
	
	if (!list_empty(&free_list)) { // free page exists
		// get free page and link
		struct list_head *free_entry = free_list.prev;
		struct list_node *free_ln = list_entry(free_entry, struct list_node, list);
		free_ln->user_va = user_va;
		free_ln->vma = vma;
		pp = virt_to_page(free_ln->slab_va);
		vmf->page = pp;
		get_page(vmf->page);

		// move to alloc_list
		list_del_init(free_entry);
		list_add(free_entry, &alloc_list);
	}
	else { // no free page, evict
		// get to-be evicted page (FIFO)
		struct list_head *evict_entry = alloc_list.prev;
		struct list_node *evict_ln = list_entry(evict_entry, struct list_node, list);

		// TODO: RDMA write that page to another node (where? need synchronous communication)
#if(USE_RDMA)
		//server_ask_free(node, &slab, &page);
		server_rdma_write(evict_ln->slab_no, evict_ln->slab_pgoff, 0, 0);
#endif

		// save page info in hash table
		// TODO: need to change node & node_va (with compiler help?)
		if (rm_ht_put(user_va, 2, 0, 0, evict_ln) != 0) { // user_va, node, slab_no, slab_pgoff, ln
			printk("Out of memory while kmallocing struct remote_map\n");
			ret = VM_FAULT_OOM;
			goto out;
		}

		// TODO: remove page table entry of evicted page's user va
		memory_remove_pte(evict_ln);

		// link page
		evict_ln->user_va = user_va;
		evict_ln->vma = vma;
		pp = virt_to_page(evict_ln->slab_va);
		vmf->page = pp;
		get_page(vmf->page);
	}

	/* Check if page is stored in remote node,
	   RDMA read if so */
	struct remote_map *remote_rm = rm_ht_get(user_va);
	struct list_node *local_ln = remote_rm->ln;

	if (remote_rm != NULL && remote_rm->node != 1) {
		// TODO: RDMA read from remote node
#if(USE_RDMA)
		server_rdma_read(local_ln->slab_no, local_ln->slab_pgoff, remote_rm->slab_no, remote_rm->slab_pgoff);
#endif
	}

	return 0;

out:
	up(&sem);
	return ret;
}

void memory_remove_pte(struct list_node *ln) {

	unsigned long del_user_va = ln->user_va;
	unsigned long del_slab_va = ln->slab_va;
	unsigned long del_pfn = page_to_pfn(virt_to_page(del_slab_va));
	struct vm_area_struct *del_vma = ln->vma;
	struct mm_struct *del_mm = del_vma->vm_mm;

	// DEBUG
	printk("del_user_va: %p\n", del_user_va);
	printk("del_slab_va: %p\n", del_slab_va);
	printk("del_pfn: %p\n", del_pfn);
	printk("del_vma: %p\n", del_vma);
	printk("del_mm: %p\n", del_mm);

	// TODO: find PTE (need to be changed for x86)
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset(del_mm, del_user_va);
	if (pgd_none(*pgd) || pgd_bad(*pgd)) {
		printk("pgd error!\n");
		return;
	}

	pud = pud_offset(pgd, del_user_va);
	if (pud_none(*pud) || pud_bad(*pud)) {
		printk("pud error!\n");
		return;
	}

	pmd = pmd_offset(pud, del_user_va);
	if (pmd_none(*pmd) || pmd_bad(*pmd)) {
		printk("pmd error!\n");
		return;
	}

	ptep = pte_offset_kernel(pmd, del_user_va);
	if (!ptep) {
		printk("pte error!\n");
		return;
	}

	// DEBUG
	printk("ptep: %p\n", ptep);
	printk("pte: %p\n", *ptep);
	printk("pfn: %p\n", pte_pfn(*ptep));

	// flush cache
	flush_cache_page(del_vma, del_user_va, del_pfn);

	// clear PTE
	pte_clear(del_mm, del_user_va, ptep);

	// flush TLB
	flush_tlb_page(del_vma, del_user_va);
}

int memory_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

	int ret = 0;
	struct mmap_page *mp;
	struct page_node *pn;
	struct list_head *lh;

	switch(cmd) {
	case 5:
		printk("ioctl: test hash table\n");

		mp_ht_init();

		ret = mp_ht_put(1000, 9, 23, NULL);
		ret = mp_ht_put(1000, 1, 3, NULL);
		ret = mp_ht_put(1000, 5, 10, NULL);
		ret = mp_ht_put(2024, 3, 10, NULL);
		if (ret == -1) {
			printk("error in mp_ht_put");
			goto out;
		}

		mp = mp_ht_get(1000);
		lh = &(mp->list);
	
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &(mp->list); lh = lh->next) {
				pn = list_entry(lh, struct page_node, list);
				printk("slab_no: %d, slab_pgoff: %d\n", pn->slab_no, pn->slab_pgoff);
			}
		}

		mp = mp_ht_get(2024);
		lh = &(mp->list);
	
		if (!list_empty(lh)) {
			for (lh = lh->next; lh != &(mp->list); lh = lh->next) {
				pn = list_entry(lh, struct page_node, list);
				printk("slab_no: %d, slab_pgoff: %d\n", pn->slab_no, pn->slab_pgoff);
			}
		}

		ret = mp_ht_del(2024, 5, 10);
		if (ret == -1) {
			printk("not found\n");
		}
		else if (ret == 0) {
			printk("should not be found but found??\n");
		}
		else {
			printk("wrong!\n");
		}

		ret = mp_ht_del(2024, 3, 10);
		if (ret == -1) {
			printk("should be found but not found??\n");
		} else if (ret == 0) {
			printk("deleted\n");
		} else {
			printk("wrong!\n");
		}

		mp_ht_destroy();

		break;
	case 7:
		printk("ioctl munmap\n");

		// TODO: communicate with remote nodes (send munmap address)

		// Local: move pages to free list
		struct mmap_page *mp_temp;
		struct list_head *lh;
		struct page_node *pn;
		mp_temp = mp_ht_get(arg);
		
		if (mp_temp != NULL && !list_empty(&(mp_temp->list))) {
			for (lh = (mp_temp->list).next; lh != &(mp_temp->list); lh = lh->next) {
				
			}
		}

		break;
	default:
		printk("ioctl default\n");
		break;
	}

out:
	return ret;
}
