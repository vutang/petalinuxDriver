#ifndef __PLIPCORE_H__
#define __PLIPCORE_H__

#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/param.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/string.h>
#include <linux/bitrev.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>

struct plipcore_device{
    int id;
    void *data;
    struct list_head list;

    int (*write)(struct plipcore_device* dev, unsigned addr, unsigned val);
	int (*read)(struct plipcore_device* dev, unsigned addr);

    void *priv_data;
};

typedef struct plipcore_device plipcore_dev_t;

int plip_init(void);

plipcore_dev_t* ipcore_device_register(struct platform_device *pdev, int id, int priv_size);
void ipcore_device_remove(struct platform_device* pdev);

plipcore_dev_t* ipcore_find_device(int id);

#endif