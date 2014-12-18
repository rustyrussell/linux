/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include "cxl.h"

void cxl_pci_dma_dev_setup(struct pci_dev *pdev)
{
	printk("WARNING %s", __func__);
}

int cxl_pci_probe_mode(struct pci_bus *bus)
{
	printk("WARNING %s", __func__);
	return PCI_PROBE_NORMAL;
}

int cxl_msi_check_device(struct pci_dev* pdev, int nvec, int type)
{
	printk("WARNING %s", __func__);
	return -ENODEV;
}

int cxl_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	printk("WARNING %s", __func__);
	return -ENODEV;
}

int cxl_teardown_msi_irqs(struct pci_dev *pdev)
{
	printk("WARNING %s", __func__);
	return -ENODEV;
}

int cxl_pci_enable_device_hook(struct pci_dev *dev)
{
	printk("WARNING %s", __func__);
	return -ENODEV;
}

resource_size_t cxl_pci_window_alignment(struct pci_bus *bus,
						unsigned long type)
{
	printk("WARNING %s", __func__);
	return -1;
}

void cxl_pci_reset_secondary_bus(struct pci_dev *dev)
{
	printk("WARNING %s", __func__);
}

static int cxl_pcie_cfg_record(u8 bus, u8 devfn)
{
	return (bus << 8) + devfn;
}

static unsigned long cxl_pcie_cfg_addr(struct pci_controller* hose,
						u8 bus, u8 devfn, int offset)
{
	int record = cxl_pcie_cfg_record(bus, devfn);

	return (unsigned long)hose->cfg_addr + ((unsigned long)hose->cfg_data * record) + offset;
}

static int cxl_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
                               int offset, int len, u32 *val)
{
        struct pci_controller *hose;
	struct cxl_afu *afu;
        volatile void __iomem *ioaddr;
	unsigned long addr;
	u32 v;

        hose = pci_bus_to_host(bus);
	afu = (struct cxl_afu *)hose->private_data;
        if (hose == NULL)
                return PCIBIOS_DEVICE_NOT_FOUND;
        if (cxl_pcie_cfg_record(bus->number, devfn) > afu->crs_num)
                return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= (unsigned long)hose->cfg_data)
                return  PCIBIOS_BAD_REGISTER_NUMBER;
        addr = cxl_pcie_cfg_addr(hose, bus->number, devfn, offset);
	ioaddr = (void *)(addr & ~0x3ULL);

	v = in_le32(ioaddr); /* can only read 32 bits */
        switch (len) {
        case 1:
		*val = (v >> ((addr & 0x3) * 8)) & 0xff;
		break;
        case 2:
		*val = (v >> ((addr & 0x2) * 8)) & 0xffff;
		break;
        default:
                *val = v;
                break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int cxl_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				 int offset, int len, u32 val)
{
        struct pci_controller *hose;
	struct cxl_afu *afu;
        volatile void __iomem *ioaddr;
	unsigned long addr;

        hose = pci_bus_to_host(bus);
	afu = (struct cxl_afu *)hose->private_data;
        if (hose == NULL)
                return PCIBIOS_DEVICE_NOT_FOUND;
        if (cxl_pcie_cfg_record(bus->number, devfn) > afu->crs_num)
                return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= (unsigned long)hose->cfg_data)
                return  PCIBIOS_BAD_REGISTER_NUMBER;
        addr = cxl_pcie_cfg_addr(hose, bus->number, devfn, offset);
	ioaddr = (void *)(addr & ~0x3ULL);

	/* FIXME ADD write!!!!! */
        return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops cxl_pcie_pci_ops =
{
	.read = cxl_pcie_read_config,
	.write = cxl_pcie_write_config,
};

int cxl_pci_phb_probe(struct cxl_afu *afu)
{
	struct pci_controller *hose;

	/* Alloc and setup PHB data structure */
//	hose = pcibios_alloc_controller(afu->dev.parent->of_node);
	hose = pcibios_alloc_controller(of_find_node_by_name(NULL, "chosen"));


	if (!hose)
		return -ENODEV;

	/* Setup parent in sysfs */
//	hose->parent = &afu->dev;

	/* Setup the PHB using arch provided callback */
	// POPULATE cfg_ops, etc...
	hose->ops = &cxl_pcie_pci_ops;
        hose->cfg_addr = afu->afu_desc_mmio + afu->crs_offset;
        hose->cfg_data = (void *)(u64)afu->crs_len;
	hose->private_data = afu;
	hose->type = PCI_CXL;

	/* Scan the bus */
	pcibios_scan_phb(hose);
	if (hose->bus == NULL)
		return -ENXIO;

	/* Claim resources. This might need some rework as well depending
	 * whether we are doing probe-only or not, like assigning unassigned
	 * resources etc...
	 */
	pcibios_claim_one_bus(hose->bus);

	/* Add probed PCI devices to the device model */
	pci_bus_add_devices(hose->bus);

	return 0;
}

struct cxl_afu *cxl_pci_to_afu(struct pci_dev *dev, unsigned int *cfg_record)
{
	struct pci_controller *hose;
	struct cxl_afu *afu;

	hose = pci_bus_to_host(dev->bus);

	afu = (struct cxl_afu *)hose->private_data;

	if (cfg_record)
		*cfg_record = cxl_pcie_cfg_record(dev->bus->number,
						  dev->devfn);

	return afu;
}
