#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#ifdef MODVERSIONS
# include <linux/modversions.h>
#endif
#include <asm/io.h>

/* character device structures */
static dev_t mmap_dev;
static struct cdev mmap_cdev;

/* methods of the character device */
static int mmap_open(struct inode *inode, struct file *filp);
static int mmap_release(struct inode *inode, struct file *filp);
static int mmap_mmap(struct file *filp, struct vm_area_struct *vma);

/* the file operations, i.e. all character device methods */
static struct file_operations mmap_fops = {
	.open = mmap_open,
	.release = mmap_release,
	.mmap = mmap_mmap,
	.owner = THIS_MODULE,
};

// internal data
// length of the two memory areas
#define NPAGES 16
// pointer to the vmalloc'd area - alway page aligned
static char *vmalloc_area;

/* character device open method */
static int mmap_open(struct inode *inode, struct file *filp)
{
	return 0;
}
/* character device last close method */
static int mmap_release(struct inode *inode, struct file *filp)
{
	return 0;
}
// helper function, mmap's the vmalloc'd area which is not physically contiguous
int mmap_vmem(struct file *filp, struct vm_area_struct *vma)
{
	int ret;
	long length = vma->vm_end - vma->vm_start;
	unsigned long start = vma->vm_start;
	char *vmalloc_area_ptr = (char *)vmalloc_area;
	unsigned long pfn;

	printk(KERN_INFO"mmap_vmem is invoked\n");
	/* check length - do not allow larger mappings than the number of
		 pages allocated */
	if (length > NPAGES * PAGE_SIZE)
		return -EIO;

	/* loop over all pages, map it page individually */
	while (length > 0) {
		pfn = vmalloc_to_pfn(vmalloc_area_ptr);
		if ((ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE,
						PAGE_SHARED)) < 0) {
			return ret;
		}
		start += PAGE_SIZE;
		vmalloc_area_ptr += PAGE_SIZE;
		length -= PAGE_SIZE;
	}
	return 0;
}

/* character device mmap method */
static int mmap_mmap(struct file *filp, struct vm_area_struct *vma)
{
	printk(KERN_INFO"mmap_mmap is invoked\n");
	/* at offset 0 we map the vmalloc'd area */
	if (vma->vm_pgoff == 0) {
		return mmap_vmem(filp, vma);
	}
#if 0
	/* at offset NPAGES we map the kmalloc'd area */
	if (vma->vm_pgoff == NPAGES) {
		return mmap_kmem(filp, vma);
	}
#endif
	/* at any other offset we return an error */
	return -EIO;
}

/* module initialization - called at module load time */
static int __init mmap_init2(void)
{
	int ret = 0;
	int i;
	char *my_char_ptr, *my_char_ptr_2;
	int *my_int_ptr;
	/* allocate a memory area with vmalloc. */
#if 1
	if ((vmalloc_area = (char *)vmalloc(NPAGES * PAGE_SIZE)) == NULL) {
		ret = -ENOMEM;
		goto out_vfree;
	}
#endif
	/* get the major number of the character device */
	if ((ret = alloc_chrdev_region(&mmap_dev, 0, 1, "mmap")) < 0) {
		printk(KERN_ERR "could not allocate major number for mmap\n");
		goto out_vfree;
	}

	/* initialize the device structure and register the device with the kernel */
	cdev_init(&mmap_cdev, &mmap_fops);
	if ((ret = cdev_add(&mmap_cdev, mmap_dev, 1)) < 0) {
		printk(KERN_ERR "could not allocate chrdev for mmap\n");
		goto out_unalloc_region;
	}
#if 0
	/* mark the pages reserved */
	for (i = 0; i < NPAGES * PAGE_SIZE; i+= PAGE_SIZE) {
		SetPageReserved(vmalloc_to_page((void *)(((unsigned long)vmalloc_area) + i)));
	}
#endif
	/* store a pattern in the memory - the test application will check for it */
#if 1
	my_char_ptr = vmalloc_area;
	memcpy(my_char_ptr," ------This is from kernel space",100); my_int_ptr = (int *)vmalloc_area + 100;
	*my_int_ptr = 1000;
	my_char_ptr_2 = (char *)((int *)vmalloc_area + 100 + 4);
	memcpy(my_char_ptr_2," ----This is second message from kernel space",100);

#endif
	return ret;

out_unalloc_region:
	unregister_chrdev_region(mmap_dev, 1);
out_vfree:
	vfree(vmalloc_area);

	return ret;
}

/* module unload */
static void __exit mmap_exit(void)
{
	int i;

	/* remove the character deivce */
	cdev_del(&mmap_cdev);
	unregister_chrdev_region(mmap_dev, 1);
#if 1
	/* unreserve the pages */
	for (i = 0; i < NPAGES * PAGE_SIZE; i+= PAGE_SIZE) {
		SetPageReserved(vmalloc_to_page((void *)(((unsigned long)vmalloc_area) + i)));
	}
#endif
	/* free the memory areas */
	vfree(vmalloc_area);
	// kfree(kmalloc_ptr);
}

module_init(mmap_init2);
module_exit(mmap_exit);
MODULE_DESCRIPTION("mmap demo driver");
MODULE_AUTHOR("Martin Frey ");
MODULE_LICENSE("Dual BSD/GPL");
