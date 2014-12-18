/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <asm/barrier.h>

#include <misc/cxl.h>
#include <uapi/misc/cxl.h>

#include "cxl-memcpy.h"

static DEFINE_PCI_DEVICE_TABLE(cxl_memcpy_pci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x4350), },
	{ }
};
MODULE_DEVICE_TABLE(pci, cxl_pci_tbl);

uint cpu_memcopy;
module_param_named(cpu_memcopy, cpu_memcopy, uint, 0600);
MODULE_PARM_DESC(cpu_memcopy, "Use CPU to perform memcpy");

#define DEVICENAME "cxlmemcpy"
#define MINOR_MAX 1
static int major_number;
static atomic_t minor_number;
static struct semaphore sem;
static struct cdev *cdev;
static dev_t dev_num;
static struct pci_dev *memcpy_afu_dev;

/* copy buffers.  This afu requires cachline alignment (ie 128 bytes) */
#define BUFFER_SIZE 1024
static char write_buf[BUFFER_SIZE] __aligned(128);
static char read_buf[BUFFER_SIZE] __aligned(128);

#define MEMCPY_QUEUE_ENTRIES 4095*2
#define MEMCPY_QUEUE_SIZE (MEMCPY_QUEUE_ENTRIES * sizeof(struct memcpy_work_element))
static struct memcpy_work_element cxl_memcpy_queue[MEMCPY_QUEUE_ENTRIES] __aligned(PAGE_SIZE);

static int cxl_memcpy_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cxl_afu *afu;
	int minor;

	afu = cxl_pci_to_afu(dev, NULL);

	minor = atomic_inc_return(&minor_number);
	if (minor >= MINOR_MAX) {
		atomic_dec(&minor_number);
		return -ENOSPC; /* we only support 1 currently */
	}
	printk("%s afu:%p; do mknod /dev/cxlmemcpy c %i 0\n", __func__, afu,
	       MAJOR(dev_num));
	sema_init(&sem, 1);
	memcpy_afu_dev = dev;
	return 0;
}

static void cxl_memcpy_remove(struct pci_dev *dev)
{
	printk("%s\n", __func__);
}

static irqreturn_t cxl_memcpy_irq_afu(int irq, void *data)
{
	struct cxl_memcpy_info *info = data;

	smp_mb();
	info->afu_irq_done = true;

	return IRQ_HANDLED;
}

static irqreturn_t cxl_memcpy_copy_error(int irq, void *data)
{
	printk("%s IRQ %i Copy error!\n", __func__, irq);
	return IRQ_HANDLED;
}

static irqreturn_t cxl_memcpy_psl_error(int irq, void *data)
{
	printk("%s IRQ %i PSL error!\n", __func__, irq);
	return IRQ_HANDLED;
}

/*
 * Setup the memcpy AFU queue.  This is memcpy AFU specific.  It needs
 * two entries, one to do the actual copy and a second the send the
 * interrupt.  The last entry is blank so the afu doesn't run off the
 * end of the queue.
 */
void cxl_memcpy_setup_queue(void)
{
	memset(cxl_memcpy_queue, 0, MEMCPY_QUEUE_SIZE);
	/* first entry: Copy */
	cxl_memcpy_queue[0].cmd = MEMCPY_WE_CMD(1, MEMCPY_WE_CMD_COPY);
	cxl_memcpy_queue[0].status = 0;
	cxl_memcpy_queue[0].length = cpu_to_be16(BUFFER_SIZE);
	cxl_memcpy_queue[0].src = cpu_to_be64((u64)write_buf);
	cxl_memcpy_queue[0].dst = cpu_to_be64((u64)read_buf);
	/* second entry: generate IRQ */
	cxl_memcpy_queue[1].cmd = MEMCPY_WE_CMD(1, MEMCPY_WE_CMD_IRQ);
	cxl_memcpy_queue[1].status = 0;
	cxl_memcpy_queue[1].length = cpu_to_be16(1);
	cxl_memcpy_queue[1].src = 0;
	cxl_memcpy_queue[1].dst = 0;
	/* third entry: left empty */

	/* Make sure this hits memory before we start the AFU */
	mb();
}


/*
 * Start the afu context.  This is calling into the generic CXL driver code
 * (except for the contents of the WED).
 */
void cxl_memcpy_start_context(struct cxl_context *ctx)
{
	u64 wed;

	wed = MEMCPY_WED(cxl_memcpy_queue,
			 MEMCPY_QUEUE_SIZE/SMP_CACHE_BYTES);
	cxl_start_context(ctx, wed, NULL);
}

void cxl_memcpy_stop_context(struct cxl_context *ctx)
{
	cxl_stop_context(ctx);
}

/* use memcpy afu to copy write_buf[] to read_buf[] */
static int memcpy_afu(struct pci_dev *dev)
{
	struct cxl_context *ctx;
	struct cxl_memcpy_info *info;
	void __iomem *psa;

	int rc = 0;

	info = kzalloc(sizeof(struct cxl_memcpy_info), GFP_KERNEL);

	ctx = cxl_dev_context_init(memcpy_afu_dev);
	if (!ctx)
		return -ENOMEM;

	/* Allocate AFU generated interrupt handler */
	rc = cxl_allocate_afu_irqs(ctx, 3);
	if (rc)
		goto err1;

	/* Register AFU interrupt 1. */
	rc = cxl_map_afu_irq(ctx, 1, cxl_memcpy_irq_afu, info, "afu1");
	if (!rc)
		goto err2;
	/* Register AFU interrupt 2 for errors. */
	rc = cxl_map_afu_irq(ctx, 2, cxl_memcpy_copy_error, ctx, "err1");
	if (!rc)
		goto err3;
	/* Register AFU interrupt 3 for errors. */
	rc = cxl_map_afu_irq(ctx, 3, cxl_memcpy_psl_error, ctx, "err2");
	if (!rc)
		goto err4;

	/* Register for PSL errors.  TODO: implement this */
	//cxl_register_error_irq(dev, flags??, callback function, private data);

	/* Map AFU MMIO/Problem space area */
	psa = cxl_psa_map(ctx);
	if (!psa)
		goto err5;

	/* Write configuration info to the AFU PSA space */
	out_be64(psa + 0, 0x8000000000000000ULL);

	/* Setup memcpy AFU work queue */
	cxl_memcpy_setup_queue();

	/* Mark interrupt as incomplete */
	info->afu_irq_done = false;
	smp_mb();

	/* Start Context on AFU */
	cxl_memcpy_start_context(ctx);

	/*
	 * Wait for interrupt to complete.  We'd never do it this way in
	 * practice, this is just a demonstration that the AFU IRQ works.
	 */
	rc = 0;
	while(!info->afu_irq_done) {
		cpu_relax();
		rc++;
	}

	rc = 0;

	/* Copy is complete!  Lets tear things down again */
	cxl_psa_unmap(psa);
	cxl_stop_context(ctx);
err5:
	cxl_unmap_afu_irq(ctx, 3, ctx);
err4:
	cxl_unmap_afu_irq(ctx, 2, ctx);
err3:
	cxl_unmap_afu_irq(ctx, 1, info);
err2:
	cxl_free_afu_irqs(ctx);
err1:
	cxl_release_context(ctx);
	kfree(info);

	return rc;
}

static ssize_t device_read(struct file *fp, char __user *buff, size_t length,
			   loff_t *ppos)
{
	int max_bytes;
	int bytes_to_read;
	int bytes_read;
	int rc;

	if (cpu_memcopy)
		memcpy(read_buf, write_buf, BUFFER_SIZE);
	else {
		rc = memcpy_afu(memcpy_afu_dev);
		if (rc)
			return -EIO;
	}

	max_bytes = BUFFER_SIZE - *ppos;
	if(max_bytes > length)
		bytes_to_read = length;
	else
		bytes_to_read = max_bytes;

	bytes_read = bytes_to_read - copy_to_user(buff, read_buf + *ppos,
						  bytes_to_read);
	*ppos += bytes_read;
	return bytes_read;
}

static ssize_t device_write(struct file *fp, const char __user  *buff,
			    size_t length, loff_t *ppos)
{
	int max_bytes;
	int bytes_to_write;
	int bytes_writen;

	max_bytes = BUFFER_SIZE - *ppos;
	if(max_bytes > length)
		bytes_to_write = length;
	else
		bytes_to_write = max_bytes;
	bytes_writen = bytes_to_write - copy_from_user(write_buf + *ppos,
						       buff, bytes_to_write);
	*ppos += bytes_writen;
	return bytes_writen;
}

static int device_open(struct inode *inode, struct file *file)
{
	if (down_interruptible(&sem) != 0) {
		return -1;
	}
	return 0;
}

static int device_close(struct inode *inode, struct file *file) {
	up(&sem);
	return 0;
}

struct pci_driver cxl_memcpy_pci_driver = {
	.name = "cxl-memcpy",
	.id_table = cxl_memcpy_pci_tbl,
	.probe = cxl_memcpy_probe,
	.remove = cxl_memcpy_remove,
};

struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.write = device_write,
	.read = device_read,
	.release = device_close,
};

static int __init init_cxl_memcpy(void)
{
	int rc = 0;

	rc = alloc_chrdev_region(&dev_num,0,MINOR_MAX,DEVICENAME);
	if (rc < 0) {
		pr_err("failed to allocate major number\n");
		return rc;
	}
	major_number = MAJOR(dev_num);
	cdev = cdev_alloc();
	cdev->ops = &fops;
	cdev->owner = THIS_MODULE;

	rc = cdev_add(cdev,dev_num,MINOR_MAX);
	if(rc < 0) {
		printk(KERN_ALERT " device adding to the kernel failed\n");
		goto err1;
	}

	atomic_set(&minor_number, -1);

	rc = pci_register_driver(&cxl_memcpy_pci_driver);
	if (rc)
		goto err;

	return 0;
err:
	cdev_del(cdev);
err1:
	unregister_chrdev_region(dev_num, MINOR_MAX);
	return rc;

}
static void exit_cxl_memcpy(void)
{
	pci_unregister_driver(&cxl_memcpy_pci_driver);
	cdev_del(cdev);
	unregister_chrdev_region(dev_num, MINOR_MAX);
}

module_init(init_cxl_memcpy);
module_exit(exit_cxl_memcpy);


MODULE_DESCRIPTION("IBM CXL memcpy AFU");
MODULE_AUTHOR("Michael Neuling <mikey@neuling.org>>");
MODULE_LICENSE("GPL");
