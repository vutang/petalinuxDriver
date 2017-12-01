#ifndef __VIPCORE_H__
#define __VIPCORE_H__

#include <linux/kernel.h>
#include <asm/io.h>

#define VDRI_MAGIC 'R'
#define VDRI_READ_REG 		_IOR(VDRI_MAGIC, 1, struct vdri_cmd *)
#define VDRI_WRITE_REG 		_IOR(VDRI_MAGIC, 2, struct vdri_cmd *)

struct vip_state{
	int id;
	void __iomem			*regs;
};

static struct vip_state *st;

int vip_write_reg(u32 reg, u32 value);

#endif