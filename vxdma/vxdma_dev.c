#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <asm/io.h>

#include <linux/amba/xilinx_dma.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

enum VXDMA_COMMAND {
	RX_TRANSFER,
	TX_TRANSFER
};

typedef struct vxdma_dev_priv {
	struct cdev cdev;
	dev_t dev;
	struct class *cl;
} vxdma_dev_private_t;

vxdma_dev_private_t *vxdma_dev_priv; 

static struct dma_chan *tx_chan;
static struct dma_chan *rx_chan;
static struct completion tx_cmp;
static struct completion rx_cmp;
static dma_cookie_t tx_cookie;
static dma_cookie_t rx_cookie;
static dma_addr_t tx_dma_handle;
static dma_addr_t rx_dma_handle;

#define WAIT 	1
#define NO_WAIT 0

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

static void axidma_test_transfer(void)
{
	const int dma_length = 32*16; 
	int i;

	/* Step 3, allocate cached memory for the transmit and receive buffers to use for DMA
	 * zeroing the destination buffer
	 */

	char *src_dma_buffer = kmalloc(dma_length, GFP_KERNEL);
	char *dest_dma_buffer = kzalloc(dma_length, GFP_KERNEL);

	if (!src_dma_buffer || !dest_dma_buffer) {
		printk(KERN_ERR "Allocating DMA memory failed\n");
		return;
	}

	/* Initialize the source buffer with known data to allow the destination buffer to
	 * be checked for success
	 */
	for (i = 0; i < dma_length; i++) 
		src_dma_buffer[i] = i;

	/* Step 4, since the CPU is done with the buffers, transfer ownership to the DMA and don't
	 * touch the buffers til the DMA is done, transferring ownership may involve cache operations
	 */

	printk(KERN_ERR "%s: dma_map_single\n", __FUNCTION__);
	tx_dma_handle = dma_map_single(tx_chan->device->dev, src_dma_buffer, dma_length, DMA_TO_DEVICE);	
	printk(KERN_ERR "%s: tx_dma_handle: %x\n", __FUNCTION__, tx_dma_handle);
	rx_dma_handle = dma_map_single(rx_chan->device->dev, dest_dma_buffer, dma_length, DMA_FROM_DEVICE);	
	printk(KERN_ERR "%s: rx_dma_handle: %x\n", __FUNCTION__, rx_dma_handle);
	/* Prepare the DMA buffers and the DMA transactions to be performed and make sure there was not
	 * any errors
	 */
	printk(KERN_ERR "%s: axidma_prep_buffer\n", __FUNCTION__);
	rx_cookie = axidma_prep_buffer(rx_chan, rx_dma_handle, dma_length, DMA_DEV_TO_MEM, &rx_cmp);
	tx_cookie = axidma_prep_buffer(tx_chan, tx_dma_handle, dma_length, DMA_MEM_TO_DEV, &tx_cmp);

	if (dma_submit_error(rx_cookie) || dma_submit_error(tx_cookie)) {
		printk(KERN_ERR "xdma_prep_buffer error\n");
		return;
	}

	printk(KERN_INFO "Starting DMA transfers\n");

	/* Start both DMA transfers and wait for them to complete
	 */
	printk(KERN_ERR "%s: axidma_start_transfer\n", __FUNCTION__);
	axidma_start_transfer(rx_chan, &rx_cmp, rx_cookie, NO_WAIT);
	axidma_start_transfer(tx_chan, &tx_cmp, tx_cookie, WAIT);

	/* Step 10, the DMA is done with the buffers so transfer ownership back to the CPU so that
	 * any cache operations needed are done
	 */
	printk(KERN_ERR "%s: dma_unmap_single\n", __FUNCTION__);
	dma_unmap_single(rx_chan->device->dev, rx_dma_handle, dma_length, DMA_FROM_DEVICE);	
	dma_unmap_single(tx_chan->device->dev, tx_dma_handle, dma_length, DMA_TO_DEVICE);

	/* Verify the data in the destination buffer matches the source buffer 
	 */
	for (i = 0; i < dma_length; i++) {
		if (dest_dma_buffer[i] != src_dma_buffer[i]) {
			printk(KERN_INFO "DMA transfer failure");
			break;	
		}
	}

	printk(KERN_INFO "DMA bytes sent: %d\n", dma_length);

	/* Step 11, free the buffers used for DMA back to the kernel
	 */

	kfree(src_dma_buffer);
	kfree(dest_dma_buffer);	
	return;
}

bool xdma_filter(struct dma_chan *chan, void *param) {
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

static int vxaxidma_test(void) {
	dma_cap_mask_t mask;
	u32 match;
	pr_debug("%s: start\n", __FUNCTION__);	

	/* Step 1, zero out the capability mask then initialize
	 * it for a slave channel that is private
	 */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE | DMA_PRIVATE, mask);


	/* Step 2, request the transmit and receive channels for the AXI DMA
	 * from the DMA engine
	 */
	pr_debug("%s: dma_request_channel\n", __FUNCTION__);
	match = (DMA_MEM_TO_DEV & 0xFF) | XILINX_DMA_IP_DMA;
	tx_chan = dma_request_channel(mask, xdma_filter, (void *) &match);
	
	match = (DMA_DEV_TO_MEM & 0xFF) | XILINX_DMA_IP_DMA;
	rx_chan = dma_request_channel(mask, xdma_filter, (void *) &match);

	if (!rx_chan || !tx_chan) { 
		pr_debug("DMA channel request error\n");
		return -1;
	}
	pr_debug("%s: request dma channel successfully\n", __FUNCTION__);

	axidma_test_transfer();

	/* Step 12, release the DMA channels back to the DMA engine
	 */

	dma_release_channel(tx_chan);
	dma_release_channel(rx_chan);

	return 1;
};

int vxdma_dev_ioctl(struct file *f, unsigned int cmd_type) {
	int rc = 0;
	pr_debug("%s: Call vxdma_dev ioctl, TYPE: %d\n", __FUNCTION__,cmd_type);
	switch(cmd_type) {
		case RX_TRANSFER:
			pr_debug("%s: Ioctl call RX_TRANSFER\n", __FUNCTION__);			
			vxaxidma_test();
			break;
		case TX_TRANSFER:			
			pr_debug("%s: Ioctl call TX_TRANSFER\n", __FUNCTION__);			
			break;
		default:
			rc = -1;
	}

	return rc;
};

static struct file_operations vxdma_dev_fops = {
	.owner = THIS_MODULE,
	// .open = vip_open,
	// .release = vip_close,
	// .read = vip_read,
	// .write = vip_write,
	.unlocked_ioctl = vxdma_dev_ioctl,
};

static int __init vxdma_dev_init(void) {
	pr_debug("[vutt6] vxdma_dev_init\n");
	int rc;

	vxdma_dev_priv = kzalloc(sizeof(struct vxdma_dev_priv), GFP_KERNEL);
	    
	if (!vxdma_dev_priv){
		pr_debug("vxdma_dev: Unable to allocate vxdma_dev_priv\n");
		return -ENOMEM;
	}
	pr_debug("[vutt6] Allocate vxdma_dev_priv\n");

	rc = alloc_chrdev_region(&vxdma_dev_priv->dev, 0, 1, "vxdma_dev");

	if (rc){
		pr_debug("%s: Failed to register dpd chardev,err %d\n", __func__, rc);
		return rc;
	}

	if ((vxdma_dev_priv->cl = class_create(THIS_MODULE, "vxdma_dev")) == NULL){
		pr_debug("%s: class_create() fail\n", __func__);
		unregister_chrdev_region(vxdma_dev_priv->dev, 1);
		return -1;
	}

	if (device_create(vxdma_dev_priv->cl, NULL, vxdma_dev_priv->dev, NULL, "vxdma_dev") == NULL) {
		pr_debug("%s: device_create() fail\n", __func__);
		class_destroy(vxdma_dev_priv->cl);
		unregister_chrdev_region(vxdma_dev_priv->dev, 1);
		return -1;
	}

	cdev_init(&vxdma_dev_priv->cdev, &vxdma_dev_fops);
	cdev_add(&vxdma_dev_priv->cdev, vxdma_dev_priv->dev, 1);

	return 0;	
};


static int __exit vxdma_dev_exit(void) {
	cdev_del(&vxdma_dev_priv->cdev);
	device_destroy(vxdma_dev_priv->cl,vxdma_dev_priv->dev);
	class_destroy(vxdma_dev_priv->cl);
	unregister_chrdev_region(vxdma_dev_priv->dev,1);
	printk(KERN_INFO "vipvers ------- closing ...");
	printk("[vutt6] vxdma_dev_exit\n");
};

module_init(vxdma_dev_init);
module_exit(vxdma_dev_exit);

MODULE_AUTHOR("VIETTEL");
MODULE_DESCRIPTION("RRU");
MODULE_LICENSE("GPL");