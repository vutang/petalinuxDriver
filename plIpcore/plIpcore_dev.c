#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <linux/fdtable.h>

#include "plIpcore.h"
#include "plIpcore_def.h"
#define RF_MAX_DEVS 1

typedef struct{
	struct cdev cdev;
	dev_t dev;
	struct class *cl
} plip_private_t;

plip_private_t *plipdev_priv;

static int plipdev_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int plipdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

int plipdev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	// printk("%s.Receive cmd: %d\n", __func__, cmd);
	switch(cmd){
		case RRU_IOC_REG_READ: {
			plipcore_dev_t *plip_dev;
			plip_iocReg_t iocReg;
			copy_from_user(&iocReg, (plip_iocReg_t *)arg, sizeof(plip_iocReg_t));
			plip_dev = ipcore_find_device(iocReg.id);
			if (!plip_dev){
				printk("Device %d not found\n", iocReg.id);
				return -1;
			}

			if (!plip_dev->read){
				printk("Read is not support for device %d\n", plip_dev->id);
				return -1;
			}

			ret = plip_dev->read(plip_dev, iocReg.addr);
			iocReg.value = ret;
			// printk("%s.read Reg = %x from Device ID = %d\n", __func__,
				// iocReg.addr, iocReg.id);
			if (copy_to_user((plip_iocReg_t*)arg, &iocReg, sizeof(plip_iocReg_t)))
			{
				ret = -EFAULT;
			}
			
			break;
		}
		case RRU_IOC_REG_WRITE:
		{
			plipcore_dev_t *plip_dev;
			plip_iocReg_t iocReg;
			copy_from_user(&iocReg, (plip_iocReg_t *)arg, sizeof(plip_iocReg_t));
			plip_dev = ipcore_find_device(iocReg.id);

			if (!plip_dev){
				printk("Device %d not found\n", iocReg.id);
				return -1;
			}

			if (!plip_dev->write){
				printk("Write is not support for device %d\n", plip_dev->id);
				return -1;
			}

			ret = plip_dev->write(plip_dev, iocReg.addr, iocReg.value);
			
			// printk("%s.write %x to Reg = %x in Device ID = %d\n", __func__,
				// iocReg.value, iocReg.addr, iocReg.id);

			break;
		}
		default: 
			printk("Unknown command %d\n", cmd);
			break;
	}

	return ret;
}

const static struct file_operations rru_fops = {
	.open = plipdev_open,
	.release = plipdev_release,
	.unlocked_ioctl = plipdev_ioctl,
};

static int __init plipdev_init(void){
	int ret;

	plipdev_priv = kzalloc(sizeof(plip_private_t), GFP_KERNEL);

	if (!plipdev_priv)
	{
		return -ENOMEM;
	}

	ret = alloc_chrdev_region(&plipdev_priv->dev, 0, RF_MAX_DEVS, "plIpcore");

	if (ret < 0) return ret;

	if ((plipdev_priv->cl = class_create(THIS_MODULE, "chardrv")) == NULL)
	{
		unregister_chrdev_region(plipdev_priv->dev, 1);
		return -1;
	}

	if (device_create(plipdev_priv->cl, NULL, plipdev_priv->dev, NULL, "plIpcore") == NULL) 
	{
		class_destroy(plipdev_priv->cl);
		unregister_chrdev_region(plipdev_priv->dev, 1);
		return -1;
	}

	cdev_init(&plipdev_priv->cdev, &rru_fops);
	cdev_add(&plipdev_priv->cdev, plipdev_priv->dev, RF_MAX_DEVS);

	plip_init();

	return 0;
}

static void __exit plipdev_exit(void){
	if (!plipdev_priv) return;

	cdev_del(&plipdev_priv->cdev);
	unregister_chrdev_region(plipdev_priv->dev, RF_MAX_DEVS);
	kfree(plipdev_priv);
}

module_init(plipdev_init);
module_exit(plipdev_exit);


MODULE_AUTHOR("VIETTEL");
MODULE_DESCRIPTION("VIETTEL RRU");
MODULE_LICENSE("GPL");