/*
 * HiTechnic Sensor Multiplexer I2C host driver for LEGO Mindstorms EV3
 *
 * Copyright (C) 2014 David Lechner <david@lechnology.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * -----------------------------------------------------------------------------
 * Provides a host for registering EV3 analog sensors. Sensors are registered
 * either though the platform data or through the set_sensor attribute.
 * -----------------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/legoev3/legoev3_ports.h>
#include <linux/legoev3/ev3_input_port.h>

#include "ht_smux.h"

struct ht_smux_i2c_host_data {
	struct legoev3_port *in_port;
	struct legoev3_port_device *sensor;
	struct mutex sensor_mutex;
};

const struct attribute_group *ht_smux_i2c_sensor_device_type_attr_groups[] = {
	&legoev3_port_device_type_attr_grp,
	NULL
};

struct device_type ht_smux_i2c_sensor_device_type = {
	.name	= "ht-smux-i2c-sensor",
	.groups	= ht_smux_i2c_sensor_device_type_attr_groups,
	.uevent = legoev3_port_device_uevent,
};

static ssize_t store_set_sensor(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ht_smux_i2c_host_data *data = dev_get_drvdata(dev);
	struct legoev3_port_device *sensor;
	struct ht_smux_i2c_sensor_platform_data pdata;
	char name[LEGOEV3_PORT_NAME_SIZE + 1] = { 0 };
	char *blank, end;
	int res;

	if (mutex_is_locked(&data->sensor_mutex))
		return -EBUSY;

	/* credit: parameter parsing code copied from i2c_core.c */

	blank = strchr(buf, ' ');
	if (!blank) {
		dev_err(dev, "%s: Missing parameters\n", "set_sensor");
		return -EINVAL;
	}
	if (blank - buf > LEGOEV3_PORT_NAME_SIZE) {
		dev_err(dev, "%s: Invalid device name\n", "set_sensor");
		return -EINVAL;
	}
	memcpy(name, buf, blank - buf);

	/* Parse remaining parameters, reject extra parameters */
	res = sscanf(++blank, "%hhu%c", &pdata.address, &end);
	if (res < 1) {
		dev_err(dev, "%s: Can't parse I2C address\n", "set_sensor");
		return -EINVAL;
	}
	if (res > 1  && end != '\n') {
		dev_err(dev, "%s: Extra parameters\n", "set_sensor");
		return -EINVAL;
	}
	pdata.address <<= 1;

	mutex_lock(&data->sensor_mutex);
	if (data->sensor) {
		legoev3_port_device_unregister(data->sensor);
		data->sensor = NULL;
	}
	sensor = legoev3_port_device_register(name, &ht_smux_i2c_sensor_device_type,
				dev, &pdata, sizeof(pdata), data->in_port);
	if (IS_ERR(sensor))
		dev_warn(dev, "Failed to sensor %s. %ld", name, PTR_ERR(sensor));
	else
		data->sensor = sensor;
	mutex_unlock(&data->sensor_mutex);

	return count;
}

DEVICE_ATTR(set_sensor, S_IWUSR, NULL, store_set_sensor);

static struct attribute *ht_smux_i2c_host_attrs[] = {
	&dev_attr_set_sensor.attr,
	NULL
};

const struct attribute_group ht_smux_i2c_host_attr_grp = {
	.attrs = ht_smux_i2c_host_attrs,
};

const struct attribute_group *ht_smux_i2c_host_driver_attr_groups[] = {
	&ht_smux_i2c_host_attr_grp,
	NULL
};

static int ht_smux_i2c_host_probe(struct legoev3_port_device *host)
{
	struct ht_smux_i2c_host_data *data;
	struct ht_smux_i2c_host_platform_data *pdata = host->dev.platform_data;
	struct legoev3_port_device *sensor;
	struct ht_smux_i2c_sensor_platform_data sensor_pdata;
	int err;

	data = kzalloc(sizeof(struct ht_smux_i2c_host_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->in_port = host->port;
	mutex_init(&data->sensor_mutex);
	dev_set_drvdata(&host->dev, data);

	err = sysfs_create_groups(&host->dev.kobj,
					ht_smux_i2c_host_driver_attr_groups);
	if (err) {
		dev_err(&host->dev, "failed to add attribute groups");
		goto err_sysfs_create_groups;
	}

	if (pdata && pdata->inital_sensor) {
		sensor_pdata.address = pdata->address;
		sensor = legoev3_port_device_register(pdata->inital_sensor,
			&ht_smux_i2c_sensor_device_type, &host->dev,
			&sensor_pdata, sizeof(sensor_pdata), data->in_port);
		if (IS_ERR(sensor))
			dev_warn(&host->dev, "Failed to sensor %s. %ld",
				pdata->inital_sensor, PTR_ERR(sensor));
		else
			data->sensor = sensor;
	}

	return 0;

err_sysfs_create_groups:
	kfree(data);

	return err;
}

static int ht_smux_i2c_host_remove(struct legoev3_port_device *host)
{
	struct ht_smux_i2c_host_data *data = dev_get_drvdata(&host->dev);

	mutex_destroy(&data->sensor_mutex);
	if (data->sensor)
		legoev3_port_device_unregister(data->sensor);
	sysfs_remove_groups(&host->dev.kobj, ht_smux_i2c_host_driver_attr_groups);
	dev_set_drvdata(&host->dev, NULL);
	kfree(data);

	return 0;
}

struct legoev3_port_device_driver ht_smux_i2c_host_driver = {
	.probe	= ht_smux_i2c_host_probe,
	.remove	= ht_smux_i2c_host_remove,
	.driver = {
		.name	= "ht-smux-i2c-host",
		.owner	= THIS_MODULE,
	},
};
EXPORT_SYMBOL_GPL(ht_smux_i2c_host_driver);
legoev3_port_device_driver(ht_smux_i2c_host_driver);

MODULE_DESCRIPTION("HiTechnic Sensor Multiplexer I2C host driver for LEGO Mindstorms EV3");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("legoev3:ht-smux-i2c-host");
