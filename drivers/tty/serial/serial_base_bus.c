// SPDX-License-Identifier: GPL-2.0+
/*
 * Serial base bus layer for controllers
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tony Lindgren <tony@atomide.com>
 *
 * The serial core bus manages the serial core controller instances.
 */

#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "serial_base.h"

static bool serial_base_initialized;

static int serial_base_match(struct device *dev, struct device_driver *drv)
{
	int len = strlen(drv->name);

	return !strncmp(dev_name(dev), drv->name, len);
}

static struct bus_type serial_base_bus_type = {
	.name = "serial-base",
	.match = serial_base_match,
};

int serial_base_driver_register(struct device_driver *driver)
{
	driver->bus = &serial_base_bus_type;

	return driver_register(driver);
}

void serial_base_driver_unregister(struct device_driver *driver)
{
	driver_unregister(driver);
}

static int serial_base_device_init(struct uart_port *port,
				   struct device *dev,
				   struct device *parent_dev,
				   const struct device_type *type,
				   void (*release)(struct device *dev),
				   int id)
{
	if (!serial_base_initialized) {
		dev_err(port->dev, "uart_add_one_port() called before arch_initcall()?\n");
		return -EPROBE_DEFER;
	}

	device_initialize(dev);
	dev->type = type;
	dev->parent = parent_dev;
	dev->bus = &serial_base_bus_type;
	dev->release = release;

	return dev_set_name(dev, "%s.%s.%d", type->name, dev_name(port->dev), id);
}

static const struct device_type serial_ctrl_type = {
	.name = "ctrl",
};

static void serial_base_ctrl_release(struct device *dev)
{
	struct serial_ctrl_device *ctrl_dev = to_serial_base_ctrl_device(dev);

	kfree(ctrl_dev);
}

void serial_base_ctrl_device_remove(struct serial_ctrl_device *ctrl_dev)
{
	if (!ctrl_dev)
		return;

	device_del(&ctrl_dev->dev);
}

struct serial_ctrl_device *serial_base_ctrl_add(struct uart_port *port,
						struct device *parent)
{
	struct serial_ctrl_device *ctrl_dev;
	int err;

	ctrl_dev = kzalloc(sizeof(*ctrl_dev), GFP_KERNEL);
	if (!ctrl_dev)
		return ERR_PTR(-ENOMEM);

	err = serial_base_device_init(port, &ctrl_dev->dev,
				      parent, &serial_ctrl_type,
				      serial_base_ctrl_release,
				      port->ctrl_id);
	if (err)
		goto err_put_device;

	err = device_add(&ctrl_dev->dev);
	if (err)
		goto err_put_device;

	return ctrl_dev;

err_put_device:
	put_device(&ctrl_dev->dev);

	return ERR_PTR(err);
}

static const struct device_type serial_port_type = {
	.name = "port",
};

static void serial_base_port_release(struct device *dev)
{
	struct serial_port_device *port_dev = to_serial_base_port_device(dev);

	kfree(port_dev);
}

struct serial_port_device *serial_base_port_add(struct uart_port *port,
						struct serial_ctrl_device *ctrl_dev)
{
	struct serial_port_device *port_dev;
	int err;

	port_dev = kzalloc(sizeof(*port_dev), GFP_KERNEL);
	if (!port_dev)
		return ERR_PTR(-ENOMEM);

	err = serial_base_device_init(port, &port_dev->dev,
				      &ctrl_dev->dev, &serial_port_type,
				      serial_base_port_release,
				      port->line);
	if (err)
		goto err_put_device;

	port_dev->port = port;

	err = device_add(&port_dev->dev);
	if (err)
		goto err_put_device;

	return port_dev;

err_put_device:
	put_device(&port_dev->dev);

	return ERR_PTR(err);
}

void serial_base_port_device_remove(struct serial_port_device *port_dev)
{
	if (!port_dev)
		return;

	device_del(&port_dev->dev);
}

static int serial_base_init(void)
{
	int ret;

	ret = bus_register(&serial_base_bus_type);
	if (ret)
		return ret;

	ret = serial_base_ctrl_init();
	if (ret)
		goto err_bus_unregister;

	ret = serial_base_port_init();
	if (ret)
		goto err_ctrl_exit;

	serial_base_initialized = true;

	return 0;

err_ctrl_exit:
	serial_base_ctrl_exit();

err_bus_unregister:
	bus_unregister(&serial_base_bus_type);

	return ret;
}
arch_initcall(serial_base_init);

static void serial_base_exit(void)
{
	serial_base_port_exit();
	serial_base_ctrl_exit();
	bus_unregister(&serial_base_bus_type);
}
module_exit(serial_base_exit);

MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("Serial core bus");
MODULE_LICENSE("GPL");
