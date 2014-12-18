/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include <linux/slab.h>

#include "cxl.h"

struct cxl_context *cxl_dev_context_init(struct pci_dev *dev)
{
	struct cxl_afu *afu;
	struct cxl_context  *ctx;
	int rc;

	afu = cxl_pci_to_afu(dev, NULL);

	ctx = cxl_context_alloc();

	/* Assume master context? */
	rc = cxl_context_init(ctx, afu, true);
	if (rc) {
		kfree(ctx);
		return NULL;
	}

	return ctx;
}

void cxl_release_context(struct cxl_context *ctx)
{
	cxl_context_free(ctx);
}

int cxl_allocate_afu_irqs(struct cxl_context *ctx, int num)
{
	if (num == 0)
		num = ctx->afu->pp_irqs;
	return afu_allocate_irqs(ctx, num);
}

void cxl_free_afu_irqs(struct cxl_context *ctx)
{
	cxl_release_irq_ranges(&ctx->irqs, ctx->afu->adapter);
}

static irq_hw_number_t cxl_find_afu_irq(struct cxl_context *ctx, int num)
{
	__u16 range;
	int r;

	WARN_ON(num == 0);

	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		range = ctx->irqs.range[r];
		if (num < range) {
			return ctx->irqs.offset[r] + num;
		}
		num -= range;
	}
	return 0;
}

int cxl_map_afu_irq(struct cxl_context *ctx, int num,
		    irq_handler_t handler, void *cookie, char *name)
{
	irq_hw_number_t hwirq;

	/*
	 * Find interrupt we are to register.
	 */
	hwirq = cxl_find_afu_irq(ctx, num);
	if (!hwirq)
		return -ENOENT;

	return cxl_map_irq(ctx->afu->adapter, hwirq, handler, cookie, name);
}

void cxl_unmap_afu_irq(struct cxl_context *ctx, int num, void *cookie)
{
	irq_hw_number_t hwirq;
	unsigned int virq;

	hwirq = cxl_find_afu_irq(ctx, num);
	if (!hwirq)
		return;

	virq = irq_find_mapping(NULL, hwirq);
	if (virq)
		cxl_unmap_irq(virq, cookie);
}


/*
 * Start a context
 * Code here similar to afu_ioctl_start_work().
 */
int cxl_start_context(struct cxl_context *ctx, u64 wed, struct pid *pid)
{
	int rc;
	bool kernel = true;

	pr_devel("%s: pe: %i\n", __func__, ctx->pe);

	mutex_lock(&ctx->status_mutex);
	if (ctx->status != OPENED) {
		rc = -EIO;
		goto out;
	}
	if (pid) {
		ctx->pid = pid;
		kernel = false;
	}

	/* FIXME: if PID userspace, then set amr here */
	if ((rc = cxl_attach_process(ctx, kernel, wed , 0)))
		goto out;

	ctx->status = STARTED;
	rc = 0;
out:
	mutex_unlock(&ctx->status_mutex);
	return rc;
}

/* Stop a context */
void cxl_stop_context(struct cxl_context *ctx)
{
	___detach_context(ctx);
}

void __iomem *cxl_psa_map(struct cxl_context *ctx)
{
	struct cxl_afu *afu = ctx->afu;
	int rc;

	rc = afu_check_and_enable(afu);
	if (rc)
		return NULL;

	pr_devel("%s: psn_phys%llx size:%llx\n",
		 __func__, afu->psn_phys, afu->adapter->ps_size);
	return ioremap(afu->psn_phys, afu->adapter->ps_size);
}

void cxl_psa_unmap(void __iomem *addr)
{
	iounmap(addr);
}
