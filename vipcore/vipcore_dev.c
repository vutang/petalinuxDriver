#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <linux/types.h>
#include <linux/uaccess.h>

#include <asm/io.h>

#include "vipcore.h"

enum VIP_COMMAND_ID {
	VIP_READ_REG,
	VIP_WRITE_REG
};

typedef struct vip_priv {
	struct cdev cdev;
	dev_t dev;
	struct class *cl;
} vip_private_t;

vip_private_t *vip_priv;

struct vip_cmd {
	unsigned int addr;
	unsigned int data;
};

static int vip_open(struct inode *i, struct file *f) {
	printk("vip_open() open file\n");
	return 0;
};

static int vip_close(struct inode *i, struct file *f) {
	printk("vip_close() close file\n");
	return 0;
};

// static inline unsigned int vdri_read(struct vipcore_local *st, unsigned reg) {
// 	return ioread32(st->base_addr + reg);
// }

// static ssize_t vip_read(struct file *f, char __user *buf, 
// 	size_t len, loff_t *off) {
// 	printk(KERN_INFO "vipvers: read()\n");
// 	return 0;
// };

// static ssize_t vip_write(struct file *f, char __user *buf,
// 	size_t len, loff_t *off) {
// 	printk(KERN_INFO "vipvers: write()\n");
// 	return len;
// };

int vip_ioctl(struct file *f, unsigned int cmd_type, unsigned long arg) {
	struct vip_cmd command;
	int rc = 0;
	/*Copy from command from user to command in kernel*/
	if (!copy_from_user(&command, (struct vip_cmd *)arg, sizeof(struct vip_cmd))) {};
	printk("[VUTT6] Call VIPCORE Ioctl, TYPE: %d, REG: %d, VAL: %d\n", 
		cmd_type, command.addr, command.data);
	switch(cmd_type) {
		case VIP_READ_REG:
			// rc = vip_read_reg(command.id, command.addr);
			// break;
		case VIP_WRITE_REG:
			// rc = vip_write_reg(command.id, command.addr, command.data);
			vip_write_reg(command.addr, command.data);
			printk("[VUTT6] Receiver Write command");
			break;
		default:
			rc = -1;
	}

	return rc;
}

static struct file_operations vip_fops = {
	.owner = THIS_MODULE,
	.open = vip_open,
	.release = vip_close,
	// .read = vip_read,
	// .write = vip_write,
	.unlocked_ioctl = vip_ioctl,
};


static int __init vip_init(void) {
	printk(KERN_INFO "[VUTT6] vipdev initializes....\n");
	int rc;

	vip_priv = kzalloc(sizeof(struct vip_priv), GFP_KERNEL);
	    
	if (!vip_priv){
		pr_debug("vipdev: Unable to allocate vip_priv\n");
		return -ENOMEM;
	}
	printk("[VUTT6] Allocate vip_priv\n");

	rc = alloc_chrdev_region(&vip_priv->dev, 0, 1, "vipdev");

	if (rc){
		pr_debug("vipdev:Failed to register dpd chardev,err %d\n", rc);
		return rc;
	}

	if ((vip_priv->cl = class_create(THIS_MODULE, "vipdev")) == NULL){
		printk("class_create() fail\n");
		unregister_chrdev_region(vip_priv->dev, 1);
		return -1;
	}

	if (device_create(vip_priv->cl, NULL, vip_priv->dev, NULL, "vipdev") == NULL) {
		printk("device_create() fail\n");
		class_destroy(vip_priv->cl);
		unregister_chrdev_region(vip_priv->dev, 1);
		return -1;
	}

	cdev_init(&vip_priv->cdev, &vip_fops);
	cdev_add(&vip_priv->cdev, vip_priv->dev, 1);

	return 0;
};

static void __exit vip_exit(void) {
	cdev_del(&vip_priv->cdev);
	device_destroy(vip_priv->cl,vip_priv->dev);
	class_destroy(vip_priv->cl);
	unregister_chrdev_region(vip_priv->dev,1);
	printk(KERN_INFO "vipvers ------- closing ...");
};

module_init(vip_init);
module_exit(vip_exit);

MODULE_AUTHOR("VIETTEL");
MODULE_DESCRIPTION("RRU");
MODULE_LICENSE("GPL");

