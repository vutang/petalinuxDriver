#ifndef __vxhlsCORE_H__
#define __vxhlsCORE_H__

#include <linux/kernel.h>
#include <asm/io.h>

#define VDRI_MAGIC 'R'
#define VDRI_READ_REG 		_IOR(VDRI_MAGIC, 1, struct vdri_cmd *)
#define VDRI_WRITE_REG 		_IOR(VDRI_MAGIC, 2, struct vdri_cmd *)

/*Regs map*/
#define XHLS_ACCEL_CONTROL_BUS_ADDR_AP_CTRL 0x0
#define XHLS_ACCEL_CONTROL_BUS_ADDR_GIE     0x1
#define XHLS_ACCEL_CONTROL_BUS_ADDR_IER     0x2
#define XHLS_ACCEL_CONTROL_BUS_ADDR_ISR     0x3

/*Control Bit*/
#define XHLS_ACCEL_CONTROL_BUS_START_BIT 	0x01

struct vxhls_state{
	int id;
	void __iomem			*regs;
};

static struct vxhls_state *st;

void vxhls_write_reg(u32 reg, u32 value);
int vxhls_read_reg(u32 reg);

#endif