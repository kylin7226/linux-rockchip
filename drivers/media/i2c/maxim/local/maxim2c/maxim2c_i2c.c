// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Dual GMSL Deserializer I2C read/write driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/i2c-mux.h>
#include <linux/delay.h>

#include "maxim2c_api.h"

/* Write registers up to 4 at a time */
int maxim2c_i2c_write(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u32 val_len, u32 reg_val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_info(&client->dev, "i2c addr(0x%02x) write: 0x%04x (%d) = 0x%08x (%d)\n",
			client->addr, reg_addr, reg_len, reg_val, val_len);

	if (val_len > 4)
		return -EINVAL;

	if (reg_len == 2) {
		buf[0] = reg_addr >> 8;
		buf[1] = reg_addr & 0xff;

		buf_i = 2;
	} else {
		buf[0] = reg_addr & 0xff;

		buf_i = 1;
	}

	val_be = cpu_to_be32(reg_val);
	val_p = (u8 *)&val_be;
	val_i = 4 - val_len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, (val_len + reg_len)) != (val_len + reg_len)) {
		dev_err(&client->dev,
			"%s: writing register 0x%04x from 0x%02x failed\n",
			__func__, reg_addr, client->addr);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(maxim2c_i2c_write);

/* Read registers up to 4 at a time */
int maxim2c_i2c_read(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u32 val_len, u32 *reg_val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg_addr);
	u8 *reg_be_p;
	int ret;

	if (val_len > 4 || !val_len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	reg_be_p = (u8 *)&reg_addr_be;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = reg_len;
	msgs[0].buf = &reg_be_p[2 - reg_len];

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = val_len;
	msgs[1].buf = &data_be_p[4 - val_len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev,
			"%s: reading register 0x%04x from 0x%02x failed\n",
			__func__, reg_addr, client->addr);
		return -EIO;
	}

	*reg_val = be32_to_cpu(data_be);

#if 0
	dev_info(&client->dev, "i2c addr(0x%02x) read: 0x%04x (%d) = 0x%08x (%d)\n",
		client->addr, reg_addr, reg_len, *reg_val, val_len);
#endif

	return 0;
}
EXPORT_SYMBOL(maxim2c_i2c_read);

/* Update registers up to 4 at a time */
int maxim2c_i2c_update(struct i2c_client *client,
		u16 reg_addr, u16 reg_len, u32 val_len, u32 val_mask, u32 reg_val)
{
	u32 value;
	int ret;

	ret = maxim2c_i2c_read(client, reg_addr, reg_len, val_len, &value);
	if (ret)
		return ret;

	value &= ~val_mask;
	value |= (reg_val & val_mask);
	ret = maxim2c_i2c_write(client, reg_addr, reg_len, val_len, value);

	return ret;
}
EXPORT_SYMBOL(maxim2c_i2c_update);

int maxim2c_i2c_write_reg(struct i2c_client *client,
			u16 reg_addr, u8 reg_val)
{
	int ret = 0;

	ret = maxim2c_i2c_write(client,
			reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_08BITS, reg_val);

	return ret;
}
EXPORT_SYMBOL(maxim2c_i2c_write_reg);

int maxim2c_i2c_read_reg(struct i2c_client *client,
			u16 reg_addr, u8 *reg_val)
{
	int ret = 0;
	u32 value = 0;
	u8 *value_be_p = (u8 *)&value;

	ret = maxim2c_i2c_read(client,
			reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
			MAXIM2C_I2C_REG_VALUE_08BITS, &value);

	*reg_val = *value_be_p;

	return ret;
}
EXPORT_SYMBOL(maxim2c_i2c_read_reg);

int maxim2c_i2c_update_reg(struct i2c_client *client,
		u16 reg_addr, u8 val_mask, u8 reg_val)
{
	u8 value;
	int ret;

	ret = maxim2c_i2c_read_reg(client, reg_addr, &value);
	if (ret)
		return ret;

	value &= ~val_mask;
	value |= (reg_val & val_mask);
	ret = maxim2c_i2c_write_reg(client, reg_addr, value);

	return ret;
}
EXPORT_SYMBOL(maxim2c_i2c_update_reg);

int maxim2c_i2c_write_array(struct i2c_client *client,
		const struct maxim2c_i2c_regval *regs)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; (ret == 0) && (regs[i].reg_addr != MAXIM2C_REG_NULL); i++) {
		if (regs[i].val_mask != 0)
			ret = maxim2c_i2c_update(client,
					regs[i].reg_addr, regs[i].reg_len,
					regs[i].val_len, regs[i].val_mask, regs[i].reg_val);
		else
			ret = maxim2c_i2c_write(client,
					regs[i].reg_addr, regs[i].reg_len,
					regs[i].val_len, regs[i].reg_val);

		if (regs[i].delay != 0)
			usleep_range(regs[i].delay * 1000, regs[i].delay * 1000 + 100);
	}

	return ret;
}
EXPORT_SYMBOL(maxim2c_i2c_write_array);

static int maxim2c_i2c_parse_init_seq(struct device *dev,
		const u8 *seq_data, int data_len, struct maxim2c_i2c_init_seq *init_seq)
{
	struct maxim2c_i2c_regval *reg_val = NULL;
	u8 *data_buf = NULL, *d8 = NULL;
	u32 i = 0;

	if ((seq_data == NULL) || (init_seq == NULL)) {
		dev_err(dev, "%s: input parameter = NULL\n", __func__);
		return -EINVAL;
	}

	if ((init_seq->seq_item_size == 0)
			|| (data_len == 0)
			|| (init_seq->reg_len == 0)
			|| (init_seq->val_len == 0)) {
		dev_err(dev, "%s: input parameter size zero\n", __func__);
		return -EINVAL;
	}

	// data_len = seq_item_size * N
	if (data_len % init_seq->seq_item_size) {
		dev_err(dev, "%s: data_len or seq_item_size error\n", __func__);
		return -EINVAL;
	}

	// seq_item_size = reg_len + val_len * 2 + 1
	if (init_seq->seq_item_size !=
			(init_seq->reg_len + init_seq->val_len * 2 + 1)) {
		dev_err(dev, "%s: seq_item_size or reg_len or val_len error\n", __func__);
		return -EINVAL;
	}

	data_buf = devm_kmemdup(dev, seq_data, data_len, GFP_KERNEL);
	if (!data_buf) {
		dev_err(dev, "%s data buf error\n", __func__);
		return -ENOMEM;
	}

	d8 = data_buf;

	init_seq->reg_seq_size = data_len / init_seq->seq_item_size;
	init_seq->reg_seq_size += 1; // add 1 for end register setting

	init_seq->reg_init_seq = devm_kcalloc(dev, init_seq->reg_seq_size,
					sizeof(struct maxim2c_i2c_regval), GFP_KERNEL);
	if (!init_seq->reg_init_seq) {
		dev_err(dev, "%s init seq buffer error\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < init_seq->reg_seq_size - 1; i++) {
		reg_val = &init_seq->reg_init_seq[i];

		reg_val->reg_len = init_seq->reg_len;
		reg_val->val_len = init_seq->val_len;

		reg_val->reg_addr = 0;
		switch (init_seq->reg_len) {
		case 4:
			reg_val->reg_addr |= (*d8 << 24);
			d8 += 1;
			fallthrough;
		case 3:
			reg_val->reg_addr |= (*d8 << 16);
			d8 += 1;
			fallthrough;
		case 2:
			reg_val->reg_addr |= (*d8 << 8);
			d8 += 1;
			fallthrough;
		case 1:
			reg_val->reg_addr |= (*d8 << 0);
			d8 += 1;
			break;
		}

		reg_val->reg_val = 0;
		switch (init_seq->val_len) {
		case 4:
			reg_val->reg_val |= (*d8 << 24);
			d8 += 1;
			fallthrough;
		case 3:
			reg_val->reg_val |= (*d8 << 16);
			d8 += 1;
			fallthrough;
		case 2:
			reg_val->reg_val |= (*d8 << 8);
			d8 += 1;
			fallthrough;
		case 1:
			reg_val->reg_val |= (*d8 << 0);
			d8 += 1;
			break;
		}

		reg_val->val_mask = 0;
		switch (init_seq->val_len) {
		case 4:
			reg_val->val_mask |= (*d8 << 24);
			d8 += 1;
			fallthrough;
		case 3:
			reg_val->val_mask |= (*d8 << 16);
			d8 += 1;
			fallthrough;
		case 2:
			reg_val->val_mask |= (*d8 << 8);
			d8 += 1;
			fallthrough;
		case 1:
			reg_val->val_mask |= (*d8 << 0);
			d8 += 1;
			break;
		}

		reg_val->delay = *d8;
		d8 += 1;
	}

	// End register setting
	init_seq->reg_init_seq[init_seq->reg_seq_size - 1].reg_len = init_seq->reg_len;
	init_seq->reg_init_seq[init_seq->reg_seq_size - 1].reg_addr = MAXIM2C_REG_NULL;

	return 0;
}

int maxim2c_i2c_load_init_seq(struct device *dev,
		struct device_node *node, struct maxim2c_i2c_init_seq *init_seq)
{
	const void *init_seq_data = NULL;
	u32 seq_data_len = 0, value = 0;
	int ret = 0;

	if ((node == NULL) || (init_seq == NULL)) {
		dev_err(dev, "%s input parameter error\n", __func__);
		return -EINVAL;
	}

	init_seq->reg_init_seq = NULL;
	init_seq->reg_seq_size = 0;

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);

		return 0;
	}

	init_seq_data = of_get_property(node, "init-sequence", &seq_data_len);
	if (!init_seq_data) {
		dev_err(dev, "failed to get property init-sequence\n");
		return -EINVAL;
	}
	if (seq_data_len == 0) {
		dev_err(dev, "init-sequence date is empty\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "seq-item-size", &value);
	if (ret) {
		dev_err(dev, "failed to get property seq-item-size\n");
		return -EINVAL;
	} else {
		dev_info(dev, "seq-item-size property: %d", value);
		init_seq->seq_item_size = value;
	}

	ret = of_property_read_u32(node, "reg-addr-len", &value);
	if (ret) {
		dev_err(dev, "failed to get property reg-addr-len\n");
		return -EINVAL;
	} else {
		dev_info(dev, "reg-addr-len property: %d", value);
		init_seq->reg_len = value;
	}

	ret = of_property_read_u32(node, "reg-val-len", &value);
	if (ret) {
		dev_err(dev, "failed to get property reg-val-len\n");
		return -EINVAL;
	} else {
		dev_info(dev, "reg-val-len property: %d", value);
		init_seq->val_len = value;
	}

	ret = maxim2c_i2c_parse_init_seq(dev,
			init_seq_data, seq_data_len, init_seq);
	if (ret) {
		dev_err(dev, "failed to parse init-sequence\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(maxim2c_i2c_load_init_seq);

int maxim2c_i2c_run_init_seq(struct i2c_client *client,
			struct maxim2c_i2c_init_seq *init_seq)
{
	int ret = 0;

	if (init_seq == NULL || init_seq->reg_init_seq == NULL)
		return 0;

	ret = maxim2c_i2c_write_array(client,
			init_seq->reg_init_seq);

	return ret;
}
EXPORT_SYMBOL(maxim2c_i2c_run_init_seq);

static int __maybe_unused maxim2c_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	maxim2c_t *maxim2c = i2c_mux_priv(muxc);
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	int ret = 0;

	dev_dbg(dev, "maxim2c i2c mux select chan = %d\n", chan);

	/* Channel select is disabled when configured in the disabled state. */
	if (maxim2c->i2c_mux.mux_disable) {
		dev_err(dev, "maxim2c i2c mux is disabled, select error\n");
		return 0;
	}

	if (maxim2c->i2c_mux.mux_channel == chan)
		return 0;

	maxim2c->i2c_mux.mux_channel = chan;

	ret = maxim2c_link_select_remote_control(maxim2c, BIT(chan));
	if (ret) {
		dev_err(dev, "maxim2c link select remote control error\n");
		return ret;
	}

	return 0;
}

static int __maybe_unused maxim2c_i2c_mux_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	maxim2c_t *maxim2c = i2c_mux_priv(muxc);
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	int ret = 0;

	dev_dbg(dev, "maxim2c i2c mux deselect chan = %d\n", chan);

	/* Channel deselect is disabled when configured in the disabled state. */
	if (maxim2c->i2c_mux.mux_disable) {
		dev_err(dev, "maxim2c i2c mux is disabled, deselect error\n");
		return 0;
	}

	ret = maxim2c_link_select_remote_control(maxim2c, 0);
	if (ret) {
		dev_err(dev, "maxim2c link select remote control error\n");
		return ret;
	}

	return 0;
}

int maxim2c_i2c_mux_disable(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	int ret = 0;

	dev_info(dev, "maxim2c i2c mux disable\n");

	ret = maxim2c_link_select_remote_control(maxim2c, 0xff);
	if (ret) {
		dev_err(dev, "maxim2c link select remote control error\n");
		return ret;
	}

	maxim2c->i2c_mux.mux_disable = true;
	return 0;
}
EXPORT_SYMBOL(maxim2c_i2c_mux_disable);

int maxim2c_i2c_mux_enable(maxim2c_t *maxim2c, u8 def_mask)
{
	struct device *dev = &maxim2c->client->dev;
	int ret = 0;

	dev_info(dev, "maxim2c i2c mux enable, mask = 0x%02x\n", def_mask);

	ret = maxim2c_link_select_remote_control(maxim2c, def_mask);
	if (ret) {
		dev_err(dev, "maxim2c link select remote control error\n");
		return ret;
	}

	maxim2c->i2c_mux.mux_disable = false;
	maxim2c->i2c_mux.mux_channel = -1;

	return 0;
}
EXPORT_SYMBOL(maxim2c_i2c_mux_enable);

static u32 maxim2c_i2c_mux_mask(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	struct device_node *i2c_mux;
	struct device_node *node = NULL;
	u32 i2c_mux_mask = 0;

	/* Balance the of_node_put() performed by of_find_node_by_name(). */
	of_node_get(dev->of_node);
	i2c_mux = of_find_node_by_name(dev->of_node, "i2c-mux");
	if (!i2c_mux) {
		dev_err(dev, "Failed to find i2c-mux node\n");
		return -EINVAL;
	}

	/* Identify which i2c-mux channels are enabled */
	for_each_child_of_node(i2c_mux, node) {
		u32 id = 0;

		of_property_read_u32(node, "reg", &id);
		if (id >= MAXIM2C_LINK_ID_MAX)
			continue;

		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled I2C bus port %u\n", id);
			continue;
		}

		i2c_mux_mask |= BIT(id);
	}
	of_node_put(node);
	of_node_put(i2c_mux);

	return i2c_mux_mask;
}

int maxim2c_i2c_mux_init(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	u32 i2c_mux_mask = 0;
	int i = 0;
	int ret = 0;

	dev_info(dev, "maxim2c i2c mux init\n");

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	maxim2c->i2c_mux.muxc = i2c_mux_alloc(client->adapter, dev,
				MAXIM2C_LINK_ID_MAX, 0, I2C_MUX_LOCKED,
				maxim2c_i2c_mux_select, NULL);
	if (!maxim2c->i2c_mux.muxc)
		return -ENOMEM;
	maxim2c->i2c_mux.muxc->priv = maxim2c;

	for (i = 0; i < MAXIM2C_LINK_ID_MAX; i++) {
		ret = i2c_mux_add_adapter(maxim2c->i2c_mux.muxc, 0, i, 0);
		if (ret) {
			i2c_mux_del_adapters(maxim2c->i2c_mux.muxc);
			return ret;
		}
	}

	i2c_mux_mask = maxim2c_i2c_mux_mask(maxim2c);
	maxim2c->i2c_mux.i2c_mux_mask = i2c_mux_mask;
	dev_info(dev, "maxim2c i2c mux mask = 0x%x\n", i2c_mux_mask);

	return 0;
}
EXPORT_SYMBOL(maxim2c_i2c_mux_init);

int maxim2c_i2c_mux_deinit(maxim2c_t *maxim2c)
{
	if (maxim2c->i2c_mux.muxc)
		i2c_mux_del_adapters(maxim2c->i2c_mux.muxc);

	maxim2c->i2c_mux.i2c_mux_mask = 0;
	maxim2c->i2c_mux.mux_disable = false;
	maxim2c->i2c_mux.mux_channel = -1;

	return 0;
}
EXPORT_SYMBOL(maxim2c_i2c_mux_deinit);
