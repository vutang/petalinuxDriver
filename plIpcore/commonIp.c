#include "plIpcore.h"
#include "plIpcore_utils.h"
#include "plIpcore_def.h"

struct commonIp_info{
	unsigned	interp_factor;
	unsigned	fcenter_shift;
};

static const struct of_device_id commonIpcore_of_match[] = {
	{ .compatible = "xlnx,capture-controller-1.0", .data = IPCORE_CAPCTL},
	{ .compatible = "xlnx,fpga-release-note-v3-3-3.3", .data = IPCORE_FPGAINFO},
	{ .compatible = "xlnx,led-controller-1.0", .data = IPCORE_LEDCTL},
	{},
};
MODULE_DEVICE_TABLE(of, commonIpcore_of_match);

static int plIpcore_probe(struct platform_device *pdev){
	struct of_device_id *id;
	plipcore_dev_t *plip_dev;
	
	printk("Probing device %s...\n", pdev->dev.of_node->name);

	id = of_match_device(commonIpcore_of_match, &pdev->dev);
	if (!id)
		return -ENODEV;

	printk("%s.add ipcore dev to dev_list\n", __func__);
	plip_dev = ipcore_device_register(pdev,  id->data, sizeof(struct commonIp_info));

	if (!plip_dev){
		return -1;
	}

	plip_dev->write = ip_core_write;
	plip_dev->read = ip_core_read;

	return 0;
}

static int plIpcore_remove(struct platform_device *pdev){
	printk("%s.remove ipcore dev from dev_list\n", __func__);
	ipcore_device_remove(pdev);
	return 0;
}

static struct platform_driver axidds_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
		.of_match_table = commonIpcore_of_match,
	},
	.probe		= plIpcore_probe,
	.remove		= plIpcore_remove,
};


module_platform_driver(axidds_driver);

MODULE_AUTHOR("viettq20@viettel.com.vn");
MODULE_DESCRIPTION("FPGA Core Driver");
MODULE_LICENSE("GPL v2");