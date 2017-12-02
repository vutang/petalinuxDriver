#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <asm/io.h>

#include <linux/amba/xilinx_dma.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#include "vxhls.h"

#define DEV_NAME "vxhls_dev"
#define MTRX_DIM 32
#define MTRX_SIZE (MTRX_DIM * MTRX_DIM)
#define DMA_LENGTH MTRX_SIZE * 4
#define WAIT 	1
#define NO_WAIT 0

enum VXHLS_COMMAND_ID {
	VXHLS_TEST,
	VXHLS_READ_REG,
	VXHLS_WRITE_REG
};

enum HLS_INPUT {
	INPUT_A,
	INPUT_B
};
typedef struct vxhls_priv {
	struct cdev cdev;
	dev_t dev;
	struct class *cl;
} vxhls_private_t;

vxhls_private_t *vxhls_priv;

struct vxhls_cmd {
	unsigned int addr;
	unsigned int data;
};

typedef int data_t;

/*DMA Channel: for DMA Setup*/
static struct dma_chan *tx_chan;
static struct dma_chan *rx_chan;
static struct completion tx_cmp;
static struct completion rx_cmp;
static dma_cookie_t tx_cookie;
static dma_cookie_t rx_cookie;
static dma_addr_t tx_dma_handle;
static dma_addr_t rx_dma_handle;

static int vxhls_open(struct inode *i, struct file *f) {
	printk("vxhls_open() open file\n");
	return 0;
};

static int vxhls_close(struct inode *i, struct file *f) {
	printk("vxhls_close() close file\n");
	return 0;
};

static int xhls_setup_accel(void) {
	u32 data;
	/*Enable Auto start*/
	vxhls_write_reg(XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL, 0x80);

	/*Disable Interrupt*/
	data = vxhls_read_reg(XHLS_ACCEL_CONTROL_BUS_ADDR_IER);
	vxhls_write_reg(XHLS_ACCEL_CONTROL_BUS_ADDR_IER, 0);

	/*Send START Signal*/
	data = vxhls_read_reg(XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL);
	printk("XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL: %x\n", data);
	vxhls_write_reg(XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL, (data & 0x80) | XHLS_ACCEL_CONTROL_BUS_AP_START_BIT);
	data = vxhls_read_reg(XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL);
	printk("XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL: %x\n", data);
	udelay(100);
	if (!(vxhls_read_reg(XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL) & XHLS_ACCEL_CONTROL_BUS_AP_READY_BIT)) {
		printk("Accel is not ready\n");
		return -1;
	}
	return 1;
}

bool xdma_filter_2(struct dma_chan *chan, void *param) {
	printk("%s: is called\n", __func__);
	if ((chan == NULL) || (chan->private == NULL)) {
		return false;
	}
	else if (param == NULL) {
		return true;
	}
  	else if (*((int *)chan->private) == *(int *)param)
    	return true;

  	return false;
}

static int xhls_setup_dma(void) {
	dma_cap_mask_t mask;
	u32 match;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE | DMA_PRIVATE, mask);

	pr_debug("%s: dma_request_channel\n", __FUNCTION__);
	match = (DMA_MEM_TO_DEV & 0xFF) | XILINX_DMA_IP_DMA;
	tx_chan = dma_request_channel(mask, xdma_filter_2, (void *) &match);
	
	match = (DMA_DEV_TO_MEM & 0xFF) | XILINX_DMA_IP_DMA;
	rx_chan = dma_request_channel(mask, xdma_filter_2, (void *) &match);

	if (!rx_chan || !tx_chan) { 
		pr_debug("DMA channel request error\n");
		return -1;
	}
	pr_debug("%s: request dma channel successfully\n", __FUNCTION__);

	return 1;
}

/* Handle a callback and indicate the DMA transfer is complete to another
 * thread of control
 */
static void axidma_sync_callback(void *completion)
{
	/* Step 9, indicate the DMA transaction completed to allow the other
	 * thread of control to finish processing
	 */ 

	complete(completion);

}

/* Prepare a DMA buffer to be used in a DMA transaction, submit it to the DMA engine 
 * to queued and return a cookie that can be used to track that status of the 
 * transaction
 */
static dma_cookie_t axidma_prep_buffer(struct dma_chan *chan, dma_addr_t buf, size_t len, 
					enum dma_transfer_direction dir, struct completion *cmp) 
{
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_async_tx_descriptor *chan_desc;
	dma_cookie_t cookie;

	/* Step 5, create a buffer (channel)  descriptor for the buffer since only a  
	 * single buffer is being used for this transfer
	 */

	chan_desc = dmaengine_prep_slave_single(chan, buf, len, dir, flags);

	/* Make sure the operation was completed successfully
	 */
	if (!chan_desc) {
		printk(KERN_ERR "dmaengine_prep_slave_single error\n");
		cookie = -EBUSY;
	} else {
		chan_desc->callback = axidma_sync_callback;
		chan_desc->callback_param = cmp;

		/* Step 6, submit the transaction to the DMA engine so that it's queued
		 * up to be processed later and get a cookie to track it's status
		 */

		cookie = dmaengine_submit(chan_desc);
	
	}
	return cookie;
}

/* Start a DMA transfer that was previously submitted to the DMA engine and then
 * wait for it complete, timeout or have an error
 */
static void axidma_start_transfer(struct dma_chan *chan, struct completion *cmp, 
					dma_cookie_t cookie, int wait)
{
	unsigned long timeout = msecs_to_jiffies(3000);
	enum dma_status status;

	/* Step 7, initialize the completion before using it and then start the 
	 * DMA transaction which was previously queued up in the DMA engine
	 */

	init_completion(cmp);
	dma_async_issue_pending(chan);

	if (wait) {
		printk("Waiting for DMA to complete...\n");

		/* Step 8, wait for the transaction to complete, timeout, or get
		 * get an error
		 */

		timeout = wait_for_completion_timeout(cmp, timeout);
		status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);

		/* Determine if the transaction completed without a timeout and
		 * withtout any errors
		 */
		if (timeout == 0)  {
			printk(KERN_ERR "DMA timed out\n");
		} else if (status != DMA_COMPLETE) {
			printk(KERN_ERR "DMA returned completion callback status of: %s\n",
			       status == DMA_ERROR ? "error" : "in progress");
		}
	}
}

static int xhls_wait_output(int *buffer) {
	rx_dma_handle = dma_map_single(rx_chan->device->dev, buffer, DMA_LENGTH, DMA_FROM_DEVICE);	
	printk(KERN_ERR "%s: rx_dma_handle: %x\n", __FUNCTION__, rx_dma_handle);

	printk(KERN_ERR "%s: axidma_prep_buffer\n", __FUNCTION__);
	rx_cookie = axidma_prep_buffer(rx_chan, rx_dma_handle, DMA_LENGTH, DMA_DEV_TO_MEM, &rx_cmp);
	if (dma_submit_error(rx_cookie)) {
		printk(KERN_ERR "xdma_prep_buffer error\n");
		return -1;
	}

	printk(KERN_ERR "%s: axidma_start_transfer\n", __FUNCTION__);
	axidma_start_transfer(rx_chan, &rx_cmp, rx_cookie, WAIT);

	printk(KERN_ERR "%s: dma_unmap_single\n", __FUNCTION__);
	dma_unmap_single(rx_chan->device->dev, rx_dma_handle, DMA_LENGTH, DMA_FROM_DEVICE);	
	return 1;
}

static int xhls_load_input(int index, int *data) {
	if (data == NULL) {
		pr_err("%s: data in is null\n", __func__);
		return -1;
	}

	printk(KERN_ERR "%s: dma_map_single\n", __FUNCTION__);
	tx_dma_handle = dma_map_single(tx_chan->device->dev, data, 
										DMA_LENGTH, DMA_TO_DEVICE);	

	printk(KERN_ERR "%s: axidma_prep_buffer\n", __FUNCTION__);
	tx_cookie = axidma_prep_buffer(tx_chan, tx_dma_handle, DMA_LENGTH, DMA_MEM_TO_DEV, &tx_cmp);

	printk(KERN_ERR "%s: check dma_submit_error\n", __FUNCTION__);
	if (dma_submit_error(tx_cookie)) {
		printk(KERN_ERR "xdma_prep_buffer error\n");
		return -1;
	}

	printk(KERN_ERR "%s: axidma_start_transfer\n", __FUNCTION__);
	axidma_start_transfer(tx_chan, &tx_cmp, tx_cookie, WAIT);

	printk(KERN_ERR "%s: dma_unmap_single\n", __FUNCTION__);
	dma_unmap_single(tx_chan->device->dev, tx_dma_handle, DMA_LENGTH, DMA_TO_DEVICE);
	return 1;
}

static int xhls_test(void) {
	int i, j, ret;
	data_t f = 0;
	data_t *input_a_dat, *input_b_dat, *ouput_dat, *iptr0, *iptr1;

	input_a_dat = kmalloc(MTRX_SIZE * sizeof(data_t), GFP_KERNEL);
	input_b_dat = kmalloc(MTRX_SIZE * sizeof(data_t), GFP_KERNEL);
	ouput_dat = kmalloc(MTRX_SIZE * sizeof(data_t), GFP_KERNEL);

	if (!input_a_dat || !input_b_dat) {
		pr_err("%s: allocating mem for test-data fail!\n", __func__);
		return -1;
	}

	iptr0 = input_a_dat;
	/*Initialize input*/
	pr_debug("%s: Initialize Input\n", __func__);
	for (i = 0; i < MTRX_DIM; i++) {
		for (j = 0; j < MTRX_DIM; j++) {
			*iptr0 = f++;
			printk("%d ", *iptr0);
			iptr0++;
		}
		printk("\n");
	}
	pr_debug("\n");
	iptr0 = input_b_dat;
	for (i = 0; i < MTRX_DIM; i++) {
		for (j = 0; j < MTRX_DIM; j++) {
			if (i == j)
				*iptr0 = 1;
			else 
				*iptr0 = 0;
			iptr0++;
		}
	}
	pr_debug("%s: call xhls_setup_accel()\n", __func__);
	ret = xhls_setup_accel();
	if (ret < 0) {
		printk("Setup Accel fail!\n");
		// goto exit_err;
	}

	pr_debug("%s: call xhls_setup_dma()\n", __func__);
	ret = xhls_setup_dma();
	if (ret < 0) {
		goto exit_err;
	}

	pr_debug("%s: trigger tx_chan\n", __func__);
	ret = xhls_load_input(INPUT_A, input_a_dat);
	if (ret < 0) {
		pr_err("%s: Can not transfer input\n", __func__);
		goto exit_err;
	}

	pr_debug("%s: trigger tx_chan\n", __func__);
	ret = xhls_load_input(INPUT_B, input_b_dat);
	if (ret < 0) {
		pr_err("%s: Can not transfer input\n", __func__);
		goto exit_err;
	}

	pr_debug("%s: trigger rx_chan\n", __func__);
	xhls_wait_output(ouput_dat);

	udelay(100);
	pr_debug("%s: Output\n", __func__);
	iptr0 = ouput_dat;
	iptr1 = input_a_dat;
	for (i = 0; i < MTRX_DIM; i++) {
		for (j = 0; j < MTRX_DIM; j++) {
			printk("%d ", *iptr0);
			// if (*iptr0 != *iptr1) {
			// 	printk("Failed\n");
			// 	goto exit_err;
			// }
			iptr0++; iptr1++;
		}
		printk("\n");
	}
	printk("\n");
	goto exit_success;
exit_err:
	if (tx_chan)
		dma_release_channel(tx_chan);
	if (rx_chan)
		dma_release_channel(rx_chan);
	kfree(input_a_dat);
	kfree(input_b_dat);
	kfree(ouput_dat);
	return -1;

exit_success:
	dma_release_channel(tx_chan);
	dma_release_channel(rx_chan);
	kfree(input_a_dat);
	kfree(input_b_dat);
	kfree(ouput_dat);
	return 1;
}

int vxhls_ioctl(struct file *f, unsigned int cmd_type) {
	int rc = 0;
	/*Copy from command from user to command in kernel*/
	// if (!copy_from_user(&command, (struct vxhls_cmd *)arg, sizeof(struct vxhls_cmd))) {};
	printk("[VUTT6] Call vxhlsCORE Ioctl, TYPE: %d\n", cmd_type);
	switch(cmd_type) {
		case VXHLS_TEST:
			xhls_test();
			break;
		default:
			rc = -1;
	}

	return rc;
}

static struct file_operations vxhls_fops = {
	.owner = THIS_MODULE,
	.open = vxhls_open,
	.release = vxhls_close,
	// .read = vxhls_read,
	// .write = vxhls_write,
	.unlocked_ioctl = vxhls_ioctl,
};


static int __init vxhls_init(void) {
	printk(KERN_INFO "%s: initializes....\n", __func__);
	int rc;

	vxhls_priv = kzalloc(sizeof(struct vxhls_priv), GFP_KERNEL);
	    
	if (!vxhls_priv){
		pr_debug("%s: Unable to allocate vxhls_priv\n", __func__);
		return -ENOMEM;
	}
	printk("[VUTT6] Allocate vxhls_priv\n");

	rc = alloc_chrdev_region(&vxhls_priv->dev, 0, 1, DEV_NAME);

	if (rc){
		pr_debug("%s: Failed to register dpd chardev,err %d\n", __func__, rc);
		return rc;
	}

	if ((vxhls_priv->cl = class_create(THIS_MODULE, DEV_NAME)) == NULL){
		pr_debug("%s: class_create() fail\n", __func__);
		unregister_chrdev_region(vxhls_priv->dev, 1);
		return -1;
	}

	if (device_create(vxhls_priv->cl, NULL, vxhls_priv->dev, NULL, DEV_NAME) == NULL) {
		pr_debug("%s: device_create() fail\n", __func__);
		class_destroy(vxhls_priv->cl);
		unregister_chrdev_region(vxhls_priv->dev, 1);
		return -1;
	}

	cdev_init(&vxhls_priv->cdev, &vxhls_fops);
	cdev_add(&vxhls_priv->cdev, vxhls_priv->dev, 1);

	return 0;
};

static void __exit vxhls_exit(void) {
	cdev_del(&vxhls_priv->cdev);
	device_destroy(vxhls_priv->cl,vxhls_priv->dev);
	class_destroy(vxhls_priv->cl);
	unregister_chrdev_region(vxhls_priv->dev,1);
	printk(KERN_INFO "vxhlsvers ------- closing ...");
};

module_init(vxhls_init);
module_exit(vxhls_exit);

MODULE_AUTHOR("VIETTEL");
MODULE_DESCRIPTION("RRU");
MODULE_LICENSE("GPL");

