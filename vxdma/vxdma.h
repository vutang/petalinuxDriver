#include <linux/amba/xilinx_dma.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

/* Register Offsets */
#define XILINX_DMA_REG_CONTROL		0x00
#define XILINX_DMA_REG_STATUS		0x04
#define XILINX_DMA_REG_CURDESC		0x08
#define XILINX_DMA_REG_CURDESCMSB	0x0C
#define XILINX_DMA_REG_TAILDESC		0x10
#define XILINX_DMA_REG_TAILDESCMSB	0x14
#define XILINX_DMA_REG_SRCDSTADDR	0x18
#define XILINX_DMA_REG_SRCDSTADDRMSB	0x1C
#define XILINX_DMA_REG_BTT		0x28

/* Channel/Descriptor Offsets */
#define XILINX_DMA_MM2S_CTRL_OFFSET	0x00
#define XILINX_DMA_S2MM_CTRL_OFFSET	0x30

/* General register bits definitions */
#define XILINX_DMA_CR_RUNSTOP_MASK	BIT(0)
#define XILINX_DMA_CR_RESET_MASK	BIT(2)
#define XILINX_DMA_CR_CYCLIC_BD_EN_MASK	BIT(4)

#define XILINX_DMA_CR_DELAY_SHIFT	24
#define XILINX_DMA_CR_COALESCE_SHIFT	16

#define XILINX_DMA_CR_DELAY_MAX		GENMASK(31, 24)
#define XILINX_DMA_CR_COALESCE_MAX	GENMASK(23, 16)

#define XILINX_DMA_SR_HALTED_MASK	BIT(0)
#define XILINX_DMA_SR_IDLE_MASK		BIT(1)

#define XILINX_DMA_XR_IRQ_IOC_MASK	BIT(12)
#define XILINX_DMA_XR_IRQ_DELAY_MASK	BIT(13)
#define XILINX_DMA_XR_IRQ_ERROR_MASK	BIT(14)
#define XILINX_DMA_XR_IRQ_ALL_MASK	GENMASK(14, 12)

/* BD definitions */
#define XILINX_DMA_BD_STS_ALL_MASK	GENMASK(31, 28)
#define XILINX_DMA_BD_SOP		BIT(27)
#define XILINX_DMA_BD_EOP		BIT(26)

/* Hw specific definitions */
#define XILINX_DMA_MAX_CHANS_PER_DEVICE	0x2
#define XILINX_DMA_MAX_TRANS_LEN	GENMASK(22, 0)

/* Delay loop counter to prevent hardware failure */
#define XILINX_DMA_LOOP_COUNT		1000000

/* Maximum number of Descriptors */
#define XILINX_DMA_NUM_DESCS		255
#define XILINX_DMA_COALESCE_MAX		255
#define XILINX_DMA_NUM_APP_WORDS	5

/**
 * struct xilinx_dma_chan - Driver specific DMA channel structure
 * @xdev: Driver specific device structure
 * @ctrl_offset: Control registers offset
 * @ctrl_reg: Control register value
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @active_list: Descriptors ready to submit
 * @done_list: Complete descriptors
 * @free_seg_list: Free descriptors
 * @common: DMA common channel
 * @seg_v: Statically allocated segments base
 * @seg_p: Physical allocated segments base
 * @dev: The dma device
 * @irq: Channel IRQ
 * @id: Channel ID
 * @has_sg: Support scatter transfers
 * @idle: Check for channel idle
 * @err: Channel has errors
 * @tasklet: Cleanup work after irq
 * @residue: Residue
 * @desc_pendingcount: Descriptor pending count
 */
struct xilinx_dma_chan {
	struct xilinx_dma_device *xdev;
	u32 ctrl_offset;
	u32 ctrl_reg;
	spinlock_t lock;
	struct list_head pending_list;
	struct list_head done_list;
	struct list_head active_list;
	struct list_head free_seg_list;
	struct dma_chan common;
	struct xilinx_dma_tx_segment *seg_v;
	dma_addr_t seg_p;
	struct device *dev;
	int irq;
	int id;
	bool has_sg;
	bool cyclic;
	int err;
	bool idle;
	struct tasklet_struct tasklet;
	u32 residue;
	u32 desc_pendingcount;
	u32 private;
	enum dma_transfer_direction direction;
};

/**
 * struct xilinx_dma_device - DMA device structure
 * @regs: I/O mapped base address
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific DMA channel
 * @has_sg: Specifies whether Scatter-Gather is present or not
 */
struct xilinx_dma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_dma_chan *chan[XILINX_DMA_MAX_CHANS_PER_DEVICE];
	bool has_sg;
};

static void xilinx_dma_start_transfer(struct xilinx_dma_chan *chan);

// int vxdma_start_transfer(struct xilinx_dma_chan *chan);

extern struct xilinx_dma_chan *vxdma_tx_chan, *vxdma_rx_chan;