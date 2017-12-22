#include "plIpcore_utils.h"

int ip_core_read(plipcore_dev_t *dev, unsigned addr){
	int ret;

	ret = ioread32(dev->data + addr);

	return ret;
}

int ip_core_write(plipcore_dev_t *dev, unsigned addr, unsigned val){

	iowrite32(val, (u32 *) dev->data + addr);

	return 0;
}