// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Linaro Ltd.
 */
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

struct platform_device *
acpi_urs_create_child_platform_device(struct platform_device *pdev,
				      struct acpi_device *child)
{
	struct resource *resources = NULL, *res;
	struct platform_device_info pdevinfo;
	struct list_head resource_list;
	struct resource_entry *rentry;
	int count;

	INIT_LIST_HEAD(&resource_list);

	count = acpi_dev_get_resources(child, &resource_list, NULL, NULL);
	if (count < 0) {
		return NULL;
	} else if (count > 0) {
		resources = devm_kcalloc(&pdev->dev,
					 pdev->num_resources + count,
					 sizeof(*resources), GFP_KERNEL);
		if (!resources) {
			acpi_dev_free_resource_list(&resource_list);
			return ERR_PTR(-ENOMEM);
		}

		/* Add URS resources  */
		memcpy(resources, pdev->resource,
		       sizeof(*resources) * pdev->num_resources);

		/* Add child's own resources */
		res = resources + pdev->num_resources;
		count = 0;
		list_for_each_entry(rentry, &resource_list, node)
			res[count++] = *rentry->res;

		acpi_dev_free_resource_list(&resource_list);
	}

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.parent = &pdev->dev;
	pdevinfo.name = dev_name(&child->dev);
	pdevinfo.id = -1;
	pdevinfo.res = resources;
	pdevinfo.num_res = count;
	pdevinfo.fwnode = acpi_fwnode_handle(child);

	if (acpi_dma_supported(child))
		pdevinfo.dma_mask = DMA_BIT_MASK(32);

	return platform_device_register_full(&pdevinfo);
}

static void acpi_urs_add_child_hid(struct acpi_device *child, const char *hid)
{
	struct acpi_device_pnp *pnp = &child->pnp;
	struct acpi_hardware_id *id;

	id = kmalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return;

	id->id = hid;
	list_add_tail(&id->list, &pnp->ids);
}

static int acpi_urs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *adev = container_of(dev->fwnode, struct acpi_device,
						fwnode);
	struct acpi_device *child;

	list_for_each_entry(child, &adev->children, node) {
		/* Make up a HID for child device, e.g. 'URS0USB0' */
		char *hid = devm_kasprintf(dev, GFP_KERNEL, "%s%s",
					   acpi_device_bid(adev),
					   acpi_device_bid(child));
		acpi_urs_add_child_hid(child, hid);
		acpi_urs_create_child_platform_device(pdev, child);
	}

	return 0;
}

static const struct acpi_device_id acpi_urs_match[] = {
	{ "QCOM0304" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, acpi_urs_acpi_match);

static struct platform_driver acpi_urs_driver = {
	.probe		= acpi_urs_probe,
	.driver		= {
		.name	= "acpi-urs",
		.acpi_match_table = ACPI_PTR(acpi_urs_match),
	},
};
module_platform_driver(acpi_urs_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm ACPI URS Glue Driver");
