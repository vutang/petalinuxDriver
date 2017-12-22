#include "plIpcore.h"
#include "plIpcore_def.h"

static int list_init = 1;
static plipcore_dev_t plip_dev_list;

plipcore_dev_t* plipcore_device_alloc(int priv_size);
void plipcore_add_device(plipcore_dev_t *dev);

int plip_init(void){
	INIT_LIST_HEAD(&plip_dev_list.list);
}

plipcore_dev_t* plipcore_device_alloc(int priv_size){
	plipcore_dev_t *plip_dev;

	plip_dev = kzalloc(sizeof(*plip_dev), GFP_KERNEL);

    if (!plip_dev){
        return NULL;
    }

    if (!priv_size){
    	plip_dev->priv_data = NULL;
    	return plip_dev;
    }


    plip_dev->priv_data = kzalloc(priv_size, GFP_KERNEL);

    if (!plip_dev->priv_data){
    	kfree(plip_dev);
    	return NULL;
    }

    return plip_dev;
}

void plipcore_device_free(plipcore_dev_t *dev){
	if (!dev){
		return;
	}

	if (dev->priv_data){
		kfree(dev->priv_data);
	}

	kfree(dev);
}

void plipcore_add_device(plipcore_dev_t *dev) {
	printk("Add rru device id = %d\n", dev->id);
	INIT_LIST_HEAD(&dev->list);
    list_add_tail(&dev->list, &plip_dev_list.list);
}

void ipcore_device_remove(struct platform_device *pdev){
	plipcore_dev_t *rru_dev;

	rru_dev = platform_get_drvdata(pdev);

	if (!rru_dev){
		return;
	}

	list_del(&rru_dev->list);

	plipcore_device_free(rru_dev);
}

plipcore_dev_t* ipcore_device_register(struct platform_device *pdev, int id, int priv_size){

	plipcore_dev_t *plip_dev;
	struct resource *mem;
	void __iomem	*regs;

	plip_dev = plipcore_device_alloc(priv_size);

    if (!plip_dev){
        return NULL;
    }

    plip_dev->id = id;
    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, mem);

	plip_dev->data = regs;

	platform_set_drvdata(pdev, plip_dev);

	plipcore_add_device(plip_dev);

	return plip_dev;
}

plipcore_dev_t* ipcore_find_device(int id){
	plipcore_dev_t *plip_dev;
	list_for_each_entry(plip_dev, &plip_dev_list.list, list) {
		if (plip_dev->id == id){
			return plip_dev;
		}
	}
	return NULL;
}