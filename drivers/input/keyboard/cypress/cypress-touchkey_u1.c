/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/earlysuspend.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include "issp_extern.h"
#include "cypress-touchkey.h"

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT540E
#include <linux/i2c/mxt540e.h>
#else
#include <linux/i2c/mxt224_u1.h>
#endif
#include <linux/i2c/touchkey_i2c.h>

/* M0 Touchkey temporary setting */

#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1VZW) || defined(CONFIG_MACH_C2)
#define CONFIG_MACH_Q1_BD
#elif defined(CONFIG_MACH_C1) && !defined(CONFIG_TARGET_LOCALE_KOR)
#define CONFIG_MACH_Q1_BD
#elif defined(CONFIG_MACH_C1) && defined(CONFIG_TARGET_LOCALE_KOR)
/* C1 KOR doesn't use Q1_BD */
#endif

#ifdef CONFIG_CM_BLN
/*
 * Standard CyanogenMod LED Notification functionality.
 */
#define ENABLE_BL        ( 1)
#define DISABLE_BL       ( 2)
#define BL_ALWAYS_ON     (-1)
#define BL_ALWAYS_OFF    (-2)

/* Breathing defaults */
#define BREATHING_STEP_INCR   (  50)
#define BREATHING_STEP_INT    ( 100)
#define BREATHING_MIN_VOLT    (2500)
#define BREATHING_MAX_VOLT    (3300)
#define BREATHING_PAUSE       ( 700)
/* Blinking defaults */
#define BLINKING_INTERVAL_ON  (1000) /* 1 second on */
#define BLINKING_INTERVAL_OFF (1000) /* 1 second off */

static int led_on = 0;
static int screen_on = 1;
static bool touch_led_control_enabled = false;
static int led_timeout = BL_ALWAYS_ON; /* never time out */
static int notification_enabled = -1; /* disabled by default */
static int notification_timeout = -1; /* never time out */
static struct wake_lock led_wake_lock;
static DEFINE_MUTEX(enable_sem);

static bool fade_out = true;
static bool breathing_enabled = false;
static bool breathe_in = true;
static unsigned int breathe_volt;

static struct breathing {
	unsigned int min;
	unsigned int max;
	unsigned int step_incr;
	unsigned int step_int;
	unsigned int pause;
} breathe = {
	.min = BREATHING_MIN_VOLT,
	.max = BREATHING_MAX_VOLT,
	.step_incr = BREATHING_STEP_INCR,
	.step_int = BREATHING_STEP_INT,
	.pause = BREATHING_PAUSE,
};

static bool blinking_enabled = false;
static bool blink_on = true;

static struct blinking {
	unsigned int int_on;
	unsigned int int_off;
} blink = {
	.int_on = BLINKING_INTERVAL_ON,
	.int_off = BLINKING_INTERVAL_OFF,
};

/* timer related declares */
static struct timer_list led_timer;
static void bl_off(struct work_struct *bl_off_work);
static DECLARE_WORK(bl_off_work, bl_off);
static struct timer_list notification_timer;
static void notification_off(struct work_struct *notification_off_work);
static DECLARE_WORK(notification_off_work, notification_off);
static struct timer_list breathing_timer;
static void breathing_timer_action(struct work_struct *breathing_off_work);
static DECLARE_WORK(breathing_off_work, breathing_timer_action);
#endif

#if defined(CONFIG_TARGET_LOCALE_NAATT_TEMP)
/* Temp Fix NAGSM_SEL_ANDROID_MOHAMMAD_ANSARI_20111224*/
#define CONFIG_TARGET_LOCALE_NAATT
#endif

static int touchkey_keycode[] = { 0,
#if defined(TK_USE_4KEY_TYPE_ATT)
	KEY_MENU, KEY_ENTER, KEY_BACK, KEY_END,

#elif defined(TK_USE_4KEY_TYPE_NA)
	KEY_SEARCH, KEY_BACK, KEY_HOME, KEY_MENU,

#elif defined(TK_USE_2KEY_TYPE_M0)
	KEY_BACK, KEY_MENU,

#else
	KEY_MENU, KEY_BACK,

#endif
};
static const int touchkey_count = sizeof(touchkey_keycode) / sizeof(int);

int touch_led_disabled = 0; // 1= force disable the touchkey backlight

#if defined(TK_HAS_AUTOCAL)
static u16 raw_data0;
static u16 raw_data1;
static u16 raw_data2;
static u16 raw_data3;
static u8 idac0;
static u8 idac1;
static u8 idac2;
static u8 idac3;
static u8 touchkey_threshold;

static int touchkey_autocalibration(struct touchkey_i2c *tkey_i2c);
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR)
#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#if defined(SEC_TKEY_EVENT_DEBUG)
static bool g_debug_tkey = TRUE;
#else
static bool g_debug_tkey = FALSE;
#endif
#endif

#if defined(CONFIG_GENERIC_BLN)
#include <linux/bln.h>
#endif
#if defined(CONFIG_GENERIC_BLN) || defined(CONFIG_CM_BLN)
#include <linux/mutex.h>
#include <linux/wakelock.h>
struct touchkey_i2c* bln_tkey_i2c;
#endif

/*
 * Generic LED Notification functionality.
 */
#ifdef CONFIG_GENERIC_BLN
struct wake_lock bln_wake_lock;
bool bln_enabled = false;
static DEFINE_MUTEX(bln_sem);
#endif

static int touchkey_i2c_check(struct touchkey_i2c *tkey_i2c);

static u8 menu_sensitivity;
static u8 back_sensitivity;
#if defined(TK_USE_4KEY)
static u8 home_sensitivity;
static u8 search_sensitivity;
#endif

static int touchkey_enable;
static bool touchkey_probe = true;

#ifdef CONFIG_TWEAK_REPLACE_BACK_MENU
static int replace_back_menu = 0;
#endif

static const struct i2c_device_id sec_touchkey_id[] = {
	{"sec_touchkey", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sec_touchkey_id);

extern int get_touchkey_firmware(char *version);
static int touchkey_led_status;
static int touchled_cmd_reversed;

static int touchkey_debug_count;
static char touchkey_debug[104];

/* led i2c write value convert helper */
static int inline touchkey_conv_led_data__(int module_ver, int data, const char* func_name) {
#if defined(CONFIG_MACH_Q1_BD) || defined(CONFIG_MACH_C1)
	if (data == 1)
		data = 0x10;
	else if (data == 2)
		data = 0x20;
#elif defined(CONFIG_TARGET_LOCALE_NA)
	if (module_ver >= 8) {
		if (data == 1)
			data = 0x10;
		else if (data == 2)
			data = 0x20;
	}
#endif
	printk("[TouchKey] %s led %s\n", func_name, (data == 0x1 || data == 0x10) ? "on" : "off");
	return data;
}
#define touchkey_conv_led_data(arg1, arg2) touchkey_conv_led_data__(arg1, arg2, __func__)

#ifdef LED_LDO_WITH_REGULATOR
static void change_touch_key_led_voltage(int vol_mv)
{
	struct regulator *tled_regulator;

	tled_regulator = regulator_get(NULL, "touch_led");
	if (IS_ERR(tled_regulator)) {
		pr_err("%s: failed to get resource %s\n", __func__,
		       "touch_led");
		return;
	}
	regulator_set_voltage(tled_regulator, vol_mv * 1000, vol_mv * 1000);
	regulator_put(tled_regulator);
}

static void get_touch_key_led_voltage(struct touchkey_i2c *tkey_i2c)
{
	struct regulator *tled_regulator;

	tled_regulator = regulator_get(NULL, "touch_led");
	if (IS_ERR(tled_regulator)) {
		pr_err("%s: failed to get resource %s\n", __func__, "touch_led");
		return;
	}

	tkey_i2c->brightness = regulator_get_voltage(tled_regulator) / 1000;

}

static ssize_t brightness_read(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	return sprintf(buf,"%d\n", tkey_i2c->brightness);
}

static ssize_t brightness_control(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int data;

	if (sscanf(buf, "%d\n", &data) == 1) {
		pr_err("[TouchKey] touch_led_brightness: %d\n", data);
		change_touch_key_led_voltage(data);
		tkey_i2c->brightness = data;
	} else {
		pr_err("[TouchKey] touch_led_brightness Error\n");
	}

	return size;
}
#endif

static void set_touchkey_debug(char value)
{
	if (touchkey_debug_count == 100)
		touchkey_debug_count = 0;

	touchkey_debug[touchkey_debug_count] = value;
	touchkey_debug_count++;
}

static int i2c_touchkey_read(struct i2c_client *client,
		u8 reg, u8 *val, unsigned int len)
{
	int err = 0;
	int retry = 3;
#if !defined(TK_USE_GENERAL_SMBUS)
	struct i2c_msg msg[1];
#endif

	if ((client == NULL) || !(touchkey_enable == 1)
	    || !touchkey_probe) {
		pr_err("[TouchKey] touchkey is not enabled. %d\n",
		       __LINE__);
		return -ENODEV;
	}

	while (retry--) {
#if defined(TK_USE_GENERAL_SMBUS)
		err = i2c_smbus_read_i2c_block_data(client,
				KEYCODE_REG, len, val);
#else
		msg->addr = client->addr;
		msg->flags = I2C_M_RD;
		msg->len = len;
		msg->buf = val;
		err = i2c_transfer(client->adapter, msg, 1);
#endif

		if (err >= 0)
			return 0;
		pr_err("[TouchKey] %s %d i2c transfer error\n",
		       __func__, __LINE__);
		mdelay(10);
	}
	return err;

}

static int i2c_touchkey_write(struct i2c_client *client,
		u8 *val, unsigned int len)
{
	int err = 0;
	int retry = 3;
#if !defined(TK_USE_GENERAL_SMBUS)
	struct i2c_msg msg[1];
#endif

	if ((client == NULL) || !(touchkey_enable == 1)
	    || !touchkey_probe) {
		pr_err("[TouchKey] touchkey is not enabled. %d\n",
		       __LINE__);
		return -ENODEV;
	}

	while (retry--) {
#if defined(TK_USE_GENERAL_SMBUS)
		err = i2c_smbus_write_i2c_block_data(client,
				KEYCODE_REG, len, val);
#else
		msg->addr = client->addr;
		msg->flags = I2C_M_WR;
		msg->len = len;
		msg->buf = val;
		err = i2c_transfer(client->adapter, msg, 1);
#endif

		if (err >= 0)
			return 0;

		pr_debug("[TouchKey] %s %d i2c transfer error\n",
		       __func__, __LINE__);
		mdelay(10);
	}
	return err;
}

#if defined(TK_HAS_AUTOCAL)
static int touchkey_autocalibration(struct touchkey_i2c *tkey_i2c)
{
	u8 data[6] = { 0, };
	int count = 0;
	int ret = 0;
	unsigned short retry = 0;

#if defined(CONFIG_TARGET_LOCALE_NA)
	if (tkey_i2c->module_ver < 8)
		return -1;
#endif

	while (retry < 3) {
		ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 4);
		if (ret < 0) {
			pr_err("[TouchKey]i2c read fail.\n");
			return ret;
		}
		pr_debug("[TouchKey] data[0]=%x data[1]=%x data[2]=%x data[3]=%x\n",
				data[0], data[1], data[2], data[3]);

		/* Send autocal Command */
		data[0] = 0x50;
		data[3] = 0x01;

		count = i2c_touchkey_write(tkey_i2c->client, data, 4);

		msleep(100);

		/* Check autocal status */
		ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 6);

		if ((data[5] & TK_BIT_AUTOCAL)) {
			pr_debug("[Touchkey] autocal Enabled\n");
			break;
		} else
			pr_debug("[Touchkey] autocal disabled, retry %d\n",
			       retry);

		retry = retry + 1;
	}

	if (retry == 3)
		pr_debug("[Touchkey] autocal failed\n");

	return count;
}
#endif

#if 0 /* CONFIG_TARGET_LOCALE_NAATT */
static ssize_t set_touchkey_autocal_testmode(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int count = 0;
	u8 set_data;
	int on_off;

	if (sscanf(buf, "%d\n", &on_off) == 1) {
		pr_err("[TouchKey] Test Mode : %d\n", on_off);

		if (on_off == 1) {
			set_data = 0x40;
			count = i2c_touchkey_write(tkey_i2c->client,
					&set_data, 1);
		} else {
			tkey_i2c->pdata->power_on(0);
			msleep(50);
			tkey_i2c->pdata->power_on(1);
			msleep(50);
#if defined(TK_HAS_AUTOCAL)
			touchkey_autocalibration();
#endif
		}
	} else {
		pr_err("[TouchKey] touch_led_brightness Error\n");
	}

	return count;
}
#endif

#if defined(TK_HAS_AUTOCAL)
static ssize_t touchkey_raw_data0_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[26] = { 0, };
	int ret;

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 26);
#if defined(CONFIG_TARGET_LOCALE_NA)
	pr_debug("called %s data[18] =%d,data[19] = %d\n", __func__,
	       data[18], data[19]);
	raw_data0 = ((0x00FF & data[18]) << 8) | data[19];
#elif defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1)\
|| defined(CONFIG_MACH_C1VZW) || defined(CONFIG_MACH_C2)
	pr_debug("called %s data[16] =%d,data[17] = %d\n", __func__,
	       data[16], data[17]);
	raw_data0 = ((0x00FF & data[16]) << 8) | data[17]; /* menu*/
#elif defined(CONFIG_MACH_Q1_BD)
	pr_debug("called %s data[16] =%d,data[17] = %d\n", __func__,
	       data[16], data[17]);
	raw_data0 = ((0x00FF & data[14]) << 8) | data[15];
#else
	pr_debug("called %s data[18] =%d,data[19] = %d\n", __func__,
	       data[10], data[11]);
	raw_data0 = ((0x00FF & data[10]) << 8) | data[11];
#endif
	return sprintf(buf, "%d\n", raw_data0);
}

static ssize_t touchkey_raw_data1_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[26] = { 0, };
	int ret;

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 26);
#if defined(CONFIG_TARGET_LOCALE_NA)
	pr_debug("called %s data[20] =%d,data[21] = %d\n", __func__,
	       data[20], data[21]);
	raw_data1 = ((0x00FF & data[20]) << 8) | data[21];
#elif defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1)\
|| defined(CONFIG_MACH_C1VZW) || defined(CONFIG_MACH_C2)
	pr_debug("called %s data[14] =%d,data[15] = %d\n", __func__,
	       data[14], data[15]);
	raw_data1 = ((0x00FF & data[14]) << 8) | data[15]; /*back*/
#elif defined(CONFIG_MACH_Q1_BD)
	pr_debug("called %s data[14] =%d,data[15] = %d\n", __func__,
			   data[14], data[15]);
	raw_data1 = ((0x00FF & data[16]) << 8) | data[17];
#else
	pr_debug("called %s data[20] =%d,data[21] = %d\n", __func__,
	       data[12], data[13]);
	raw_data1 = ((0x00FF & data[12]) << 8) | data[13];
#endif				/* CONFIG_TARGET_LOCALE_NA */
	return sprintf(buf, "%d\n", raw_data1);
}

static ssize_t touchkey_raw_data2_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[26] = { 0, };
	int ret;

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 26);
#if defined(CONFIG_TARGET_LOCALE_NA)
	pr_debug("called %s data[22] =%d,data[23] = %d\n", __func__,
	       data[22], data[23]);
	raw_data2 = ((0x00FF & data[22]) << 8) | data[23];
#else
	pr_debug("called %s data[22] =%d,data[23] = %d\n", __func__,
	       data[14], data[15]);
	raw_data2 = ((0x00FF & data[14]) << 8) | data[15];
#endif				/* CONFIG_TARGET_LOCALE_NA */
	return sprintf(buf, "%d\n", raw_data2);
}

static ssize_t touchkey_raw_data3_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[26] = { 0, };
	int ret;

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 26);
#if defined(CONFIG_TARGET_LOCALE_NA)
	pr_debug("called %s data[24] =%d,data[25] = %d\n", __func__,
	       data[24], data[25]);
	raw_data3 = ((0x00FF & data[24]) << 8) | data[25];
#else
	pr_debug("called %s data[24] =%d,data[25] = %d\n", __func__,
	       data[16], data[17]);
	raw_data3 = ((0x00FF & data[16]) << 8) | data[17];
#endif				/* CONFIG_TARGET_LOCALE_NA */
	return sprintf(buf, "%d\n", raw_data3);
}

static ssize_t touchkey_idac0_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[10];
	int ret;
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8)
		return 0;
#endif

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 10);
	pr_debug("called %s data[6] =%d\n", __func__, data[6]);
	idac0 = data[6];
	return sprintf(buf, "%d\n", idac0);
}

static ssize_t touchkey_idac1_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[10];
	int ret;
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8)
		return 0;
#endif

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 10);
	pr_debug("called %s data[7] = %d\n", __func__, data[7]);
	idac1 = data[7];
	return sprintf(buf, "%d\n", idac1);
}

static ssize_t touchkey_idac2_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[10];
	int ret;
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8)
		return 0;
#endif

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 10);
	pr_debug("called %s data[8] =%d\n", __func__, data[8]);
	idac2 = data[8];
	return sprintf(buf, "%d\n", idac2);
}

static ssize_t touchkey_idac3_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[10];
	int ret;
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8)
		return 0;
#endif

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 10);
	pr_debug("called %s data[9] = %d\n", __func__, data[9]);
	idac3 = data[9];
	return sprintf(buf, "%d\n", idac3);
}

static ssize_t touchkey_threshold_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[10];
	int ret;

	pr_debug("called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 10);
	pr_debug("called %s data[4] = %d\n", __func__, data[4]);
	touchkey_threshold = data[4];
	return sprintf(buf, "%d\n", touchkey_threshold);
}
#endif

#if defined(TK_HAS_FIRMWARE_UPDATE)
static int touchkey_firmware_update(struct touchkey_i2c *tkey_i2c)
{
	int retry = 3;
	int ret = 0;
	char data[3];

	disable_irq(tkey_i2c->irq);


	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);
	if (ret < 0) {
		pr_debug("[TouchKey] i2c read fail. do not excute firm update.\n");
		data[1] = 0;
		data[2] = 0;
	}

	pr_err("%s F/W version: 0x%x, Module version:0x%x\n", __func__,
	data[1], data[2]);

	tkey_i2c->firmware_ver = data[1];
	tkey_i2c->module_ver = data[2];

#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1) \
|| defined(CONFIG_MACH_C1VZW) || defined(CONFIG_MACH_C2)
	if ((tkey_i2c->firmware_ver < TK_FIRMWARE_VER) &&
	    (tkey_i2c->module_ver <= TK_MODULE_VER)) {
#else
	if ((tkey_i2c->firmware_ver < TK_FIRMWARE_VER) &&
		(tkey_i2c->module_ver == TK_MODULE_VER)) {
#endif
		pr_debug("[TouchKey] firmware auto update excute\n");

		tkey_i2c->update_status = TK_UPDATE_DOWN;

		while (retry--) {
			if (ISSP_main(tkey_i2c) == 0) {
				pr_debug("[TouchKey]firmware update succeeded\n");
				tkey_i2c->update_status = TK_UPDATE_PASS;
				msleep(50);
				break;
			}
			msleep(50);
			pr_debug("[TouchKey] firmware update failed. retry\n");
		}
		if (retry <= 0) {
			tkey_i2c->pdata->power_on(0);
			tkey_i2c->update_status = TK_UPDATE_FAIL;
			pr_debug("[TouchKey] firmware update failed.\n");
		}
		ret = touchkey_i2c_check(tkey_i2c);
		if (ret < 0) {
			pr_debug("[TouchKey] i2c read fail.\n");
			return TK_UPDATE_FAIL;
		}
#if defined(CONFIG_TARGET_LOCALE_KOR)
		ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);
		if (ret < 0) {
			pr_debug("[TouchKey] i2c read fail. do not excute firm update.\n");
		}
		tkey_i2c->firmware_ver = data[1];
		tkey_i2c->module_ver = data[2];
#endif
		printk(KERN_DEBUG "[TouchKey] firm ver = %d, module ver = %d\n",
			tkey_i2c->firmware_ver, tkey_i2c->module_ver);
	} else {
		pr_debug("[TouchKey] firmware auto update do not excute\n");
		pr_debug("[TouchKey] firmware_ver(banary=%d, current=%d)\n",
		       TK_FIRMWARE_VER, tkey_i2c->firmware_ver);
		pr_debug("[TouchKey] module_ver(banary=%d, current=%d)\n",
		       TK_MODULE_VER, tkey_i2c->module_ver);
	}
	enable_irq(tkey_i2c->irq);
	return TK_UPDATE_PASS;
}
#else
static int touchkey_firmware_update(struct touchkey_i2c *tkey_i2c)
{
	char data[3];
	int retry;
	int ret = 0;

	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);
	if (ret < 0) {
		pr_debug("[TouchKey] i2c read fail. do not excute firm update.\n");
		return ret;
	}

	pr_err("%s F/W version: 0x%x, Module version:0x%x\n", __func__,
	       data[1], data[2]);
	retry = 3;

	tkey_i2c->firmware_ver = data[1];
	tkey_i2c->module_ver = data[2];

	if (tkey_i2c->firmware_ver < 0x0A) {
		tkey_i2c->update_status = TK_UPDATE_DOWN;
		while (retry--) {
			if (ISSP_main(tkey_i2c) == 0) {
				pr_err("[TOUCHKEY]Touchkey_update succeeded\n");
				tkey_i2c->update_status = TK_UPDATE_PASS;
				break;
			}
			pr_err("touchkey_update failed...retry...\n");
		}
		if (retry <= 0) {
			tkey_i2c->pdata->power_on(0);
			tkey_i2c->update_status = TK_UPDATE_FAIL;
			ret = TK_UPDATE_FAIL;
		}
	} else {
		if (tkey_i2c->firmware_ver >= 0x0A) {
			pr_err("[TouchKey] Not F/W update. Cypess touch-key F/W version is latest\n");
		} else {
			pr_err("[TouchKey] Not F/W update. Cypess touch-key version(module or F/W) is not valid\n");
		}
	}
	return ret;
}
#endif

static irqreturn_t touchkey_interrupt(int irq, void *dev_id)
{
	struct touchkey_i2c *tkey_i2c = dev_id;
	u8 data[3];
	int ret;
	int retry = 10;
	int keycode_type = 0;
	int pressed;

	set_touchkey_debug('a');

	retry = 3;
	while (retry--) {
		ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);
		if (!ret)
			break;
		else {
			pr_debug("[TouchKey] i2c read failed, ret:%d, retry: %d\n",
			       ret, retry);
			continue;
		}
	}
	if (ret < 0)
		return IRQ_HANDLED;

	set_touchkey_debug(data[0]);

	keycode_type = (data[0] & TK_BIT_KEYCODE);
	pressed = !(data[0] & TK_BIT_PRESS_EV);

	if (keycode_type <= 0 || keycode_type >= touchkey_count) {
		pr_debug("[Touchkey] keycode_type err\n");
		return IRQ_HANDLED;
	}

	if (pressed)
		set_touchkey_debug('P');

	if (get_tsp_status() && pressed)
		pr_debug("[TouchKey] touchkey pressed but don't send event because touch is pressed.\n");
	else {
#ifdef CONFIG_TWEAK_REPLACE_BACK_MENU
		if (replace_back_menu) {
			if (touchkey_keycode[keycode_type] == KEY_BACK) {
				input_report_key(tkey_i2c->input_dev, KEY_MENU, pressed);
			} else if (touchkey_keycode[keycode_type] == KEY_MENU) {
				input_report_key(tkey_i2c->input_dev, KEY_BACK, pressed);
			} else {
				input_report_key(tkey_i2c->input_dev, touchkey_keycode[keycode_type], pressed);
			}
		} else
#endif
		input_report_key(tkey_i2c->input_dev,
				 touchkey_keycode[keycode_type], pressed);
		input_sync(tkey_i2c->input_dev);
		/* pr_debug("[TouchKey] keycode:%d pressed:%d\n",
		   touchkey_keycode[keycode_index], pressed); */

		#if defined(CONFIG_TARGET_LOCALE_KOR)
		if (g_debug_tkey == true) {
			pr_debug("[TouchKey] keycode[%d]=%d pressed:%d\n",
			keycode_type, touchkey_keycode[keycode_type], pressed);
		} else {
			pr_debug("[TouchKey] pressed:%d\n", pressed);
		}
		#endif
	}

#ifdef CONFIG_CM_BLN
	if (touch_led_disabled == 0) {
		/* we have timed out or the lights should be on */
		if (led_timer.expires > jiffies || led_timeout != BL_ALWAYS_OFF) {
			int data = touchkey_conv_led_data(tkey_i2c->module_ver, 1);
			change_touch_key_led_voltage(tkey_i2c->brightness);
			i2c_touchkey_write(tkey_i2c->client, (u8 *)&data, 1); /* turn on */
		}

		/* restart the timer */
		if (led_timeout > 0) {
			mod_timer(&led_timer, jiffies + msecs_to_jiffies(led_timeout));
		}
	}
#endif

	set_touchkey_debug('A');
	return IRQ_HANDLED;
}

#ifdef CONFIG_GENERIC_BLN
static void touchkey_bln_enable(void)
{
	int data;

	mutex_lock(&bln_sem);
	if (bln_enabled)
	{
		if (touchkey_enable == 0) {
			if (!wake_lock_active(&bln_wake_lock)) {
				wake_lock(&bln_wake_lock);
			}
			bln_tkey_i2c->pdata->power_on(1);
			msleep(50);
			bln_tkey_i2c->pdata->led_power_on(1);
			touchkey_enable = 1;
		}
		change_touch_key_led_voltage(bln_tkey_i2c->brightness);
		data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 1);
		i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
		touchkey_led_status = 2;
		touchled_cmd_reversed = 1;
	}
	mutex_unlock(&bln_sem);
}

static void touchkey_bln_disable(void)
{
	int data;

	mutex_lock(&bln_sem);
	if (bln_enabled)
	{
		data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 2);
		i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
		if (touchkey_enable == 1) {
			bln_tkey_i2c->pdata->led_power_on(0);
			bln_tkey_i2c->pdata->power_on(0);
			touchkey_enable = 0;
			wake_unlock(&bln_wake_lock);
		}
		touchkey_led_status = 1;
		touchled_cmd_reversed = 1;
	}
	mutex_unlock(&bln_sem);
}

static struct bln_implementation cypress_touchkey_bln = {
	.enable = touchkey_bln_enable,
	.disable = touchkey_bln_disable,
};
#endif // CONFIG_GENERIC_BLN

#ifdef CONFIG_CM_BLN
/*
 * Start of the main LED Notify code block
 */
static void enable_touchkey_backlights(void)
{
        int data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 1);
        i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
}

static void disable_touchkey_backlights(void)
{
        int data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 2);
        i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
}

static void reset_breathing(void)
{
	breathe_in = true;
	breathe_volt = breathe.min;
	if (breathing_enabled)
		change_touch_key_led_voltage(breathe.min);
}

static void led_fadeout(void)
{
	int i, data;

	for (i = bln_tkey_i2c->brightness; i >= BREATHING_MIN_VOLT; i -= 50) {
		change_touch_key_led_voltage(i);
		msleep(50);
	}

	data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 2);
	i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
}

static void bl_off(struct work_struct *bl_off_work)
{
	/* do nothing if there is an active notification */
	if (led_on || !touchkey_enable)
		return;

	/* we have timed out, turn the lights off */
	if (fade_out) {
		led_fadeout();
	} else {
		int data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 2);
		i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
	}

	return;
}

static void handle_led_timeout(unsigned long data)
{
	/* we cannot call the timeout directly as it causes a kernel spinlock BUG, schedule it instead */
	schedule_work(&bl_off_work);
}

static void notification_off(struct work_struct *notification_off_work)
{
	int data;

	/* do nothing if there is no active notification */
	if (!led_on || !touchkey_enable)
		return;

	/* we have timed out, turn the lights off */
	/* disable the regulators */
	bln_tkey_i2c->pdata->led_power_on(0);	/* "touch_led" regulator */
	bln_tkey_i2c->pdata->power_on(0);	/* "touch" regulator */

	/* turn off the backlight */
	data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 2); /* light off */
	i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
	touchkey_enable = 0;
	led_on = 0;

	/* we were using a wakelock, unlock it */
	if (wake_lock_active(&led_wake_lock)) {
		wake_unlock(&led_wake_lock);
	}

	return;
}

static void handle_notification_timeout(unsigned long data)
{
	/* we cannot call the timeout directly as it causes a kernel spinlock BUG, schedule it instead */
	schedule_work(&notification_off_work);
}

static void start_breathing_timer(void)
{
	mod_timer(&breathing_timer, jiffies + msecs_to_jiffies(10));
}

static void breathing_timer_action(struct work_struct *breathing_off_work)
{
	if (breathing_enabled && led_on) {
		if (breathe_in) {
			change_touch_key_led_voltage(breathe_volt);
			breathe_volt += breathe.step_incr;
			if (breathe_volt >= breathe.max) {
				breathe_volt = breathe.max;
				breathe_in = false;
			}
			mod_timer(&breathing_timer, jiffies + msecs_to_jiffies(breathe.step_int));
		} else {
			change_touch_key_led_voltage(breathe_volt);
			breathe_volt -= breathe.step_incr;
			if (breathe_volt <= breathe.min) {
				reset_breathing();
				mod_timer(&breathing_timer, jiffies + msecs_to_jiffies(breathe.pause));
			} else {
				mod_timer(&breathing_timer, jiffies + msecs_to_jiffies(breathe.step_int));
			}
		}
	} else if (blinking_enabled && led_on) {
		if (blink_on) {
			enable_touchkey_backlights();
			mod_timer(&breathing_timer, jiffies + msecs_to_jiffies(blink.int_on));
			blink_on = false;
		} else {
			disable_touchkey_backlights();
			mod_timer(&breathing_timer, jiffies + msecs_to_jiffies(blink.int_off));
			blink_on = true;
		}
	}

	return;
}

static void handle_breathing_timeout(unsigned long data)
{
	/* we cannot call the timeout directly as it causes a kernel spinlock BUG, schedule it instead */
	schedule_work(&breathing_off_work);
}

static ssize_t led_status_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%u\n", led_on);
}

static ssize_t led_status_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int cmd;
	int data;

	if(sscanf(buf,"%u\n", &cmd ) == 1) {

		switch (cmd) {
		case ENABLE_BL:
			printk(KERN_DEBUG "[LED] ENABLE_BL\n");
			if (notification_enabled > 0) {
				/* we are using a wakelock, activate it */
				if (!wake_lock_active(&led_wake_lock)) {
					wake_lock(&led_wake_lock);
				}

				if (!screen_on) {
					/* enable regulators */
					bln_tkey_i2c->pdata->power_on(1);	/* "touch" regulator */
					bln_tkey_i2c->pdata->led_power_on(1);	/* "touch_led" regulator */
					touchkey_enable = 1;
				}

				/* enable the backlight */
				change_touch_key_led_voltage(bln_tkey_i2c->brightness);
				data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 1);
				i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
				led_on = 1;

				/* start breathing timer */
				if (breathing_enabled || blinking_enabled) {
					reset_breathing();
					start_breathing_timer();
				}

				/* See if a timeout value has been set for the notification */
				if (notification_timeout > 0) {
					/* restart the timer */
					mod_timer(&notification_timer, jiffies + msecs_to_jiffies(notification_timeout));
				}
			}
			break;

		case DISABLE_BL:
			printk(KERN_DEBUG "[LED] DISABLE_BL\n");

			/* prevent race with late resume*/
			mutex_lock(&enable_sem);

			/* only do this if a notification is on already, do nothing if not */
			if (led_on) {

				/* turn off the backlight */
				data = touchkey_conv_led_data(bln_tkey_i2c->module_ver, 2); /* light off */
				i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
				led_on = 0;

				if (!screen_on) {
					/* disable the regulators */
					bln_tkey_i2c->pdata->led_power_on(0);	/* "touch_led" regulator */
					bln_tkey_i2c->pdata->power_on(0);	/* "touch" regulator */
					touchkey_enable = 0;
				}

				/* a notification timeout was set, disable the timer */
				if (notification_timeout > 0) {
					del_timer(&notification_timer);
				}

				/* disable the breathing timer */
				if (breathing_enabled || blinking_enabled) {
					del_timer(&breathing_timer);
				}

				/* we were using a wakelock, unlock it */
				if (wake_lock_active(&led_wake_lock)) {
					wake_unlock(&led_wake_lock);
				}
			}

			/* prevent race */
			mutex_unlock(&enable_sem);

			break;
		}
	}

	return size;
}

static ssize_t led_timeout_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", led_timeout);
}

static ssize_t led_timeout_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	sscanf(buf,"%d\n", &led_timeout);
	return size;
}

static ssize_t notification_enabled_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", notification_enabled);
}

static ssize_t notification_enabled_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	sscanf(buf,"%d\n", &notification_enabled);
	return size;
}

static ssize_t notification_timeout_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", notification_timeout);
}

static ssize_t notification_timeout_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	sscanf(buf,"%d\n", &notification_timeout);
	return size;
}

static ssize_t enable_breathing_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf, "%u\n", (breathing_enabled ? 1 : 0));
}

static ssize_t enable_breathing_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 0 || data > 1)
		return -EINVAL;

	breathing_enabled = (data ? true : false);

	if (blinking_enabled)
		blinking_enabled = false;

	return size;
}

static ssize_t breathing_step_incr_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", breathe.step_incr);
}

static ssize_t breathing_step_incr_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 10 || data > 100)
		return -EINVAL;

	breathe.step_incr = data;
	return size;
}

static ssize_t breathing_step_int_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", breathe.step_int);
}

static ssize_t breathing_step_int_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 10 || data > 100)
		return -EINVAL;

	breathe.step_int = data;
	return size;
}

static ssize_t breathing_max_volt_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", breathe.max);
}

static ssize_t breathing_max_volt_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < BREATHING_MIN_VOLT || data > BREATHING_MAX_VOLT)
		return -EINVAL;

	breathe.max = data;
	return size;
}

static ssize_t breathing_min_volt_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", breathe.min);
}

static ssize_t breathing_min_volt_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < BREATHING_MIN_VOLT || data > BREATHING_MAX_VOLT)
		return -EINVAL;

	breathe.min = data;
	return size;
}

static ssize_t breathing_pause_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", breathe.pause);
}

static ssize_t breathing_pause_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 100 || data > 5000)
		return -EINVAL;

	breathe.pause = data;
	return size;
}

static ssize_t enable_blinking_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf, "%u\n", (blinking_enabled ? 1 : 0));
}

static ssize_t enable_blinking_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 0 || data > 1)
		return -EINVAL;

	blinking_enabled = (data ? true : false);

	if (breathing_enabled)
		breathing_enabled = false;

	return size;
}

static ssize_t blinking_int_on_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", blink.int_on);
}

static ssize_t blinking_int_on_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 1 || data > 10000)
		return -EINVAL;

	blink.int_on = data;
	return size;
}

static ssize_t blinking_int_off_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf,"%d\n", blink.int_off);
}

static ssize_t blinking_int_off_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 1 || data > 10000)
		return -EINVAL;

	blink.int_off = data;
	return size;
}

static ssize_t led_fadeout_read( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf, "%u\n", (fade_out ? 1 : 0));
}

static ssize_t led_fadeout_write( struct device *dev, struct device_attribute *attr, const char *buf, size_t size )
{
	unsigned int data;
	int ret;

	ret = sscanf(buf, "%d", &data);
	if (ret != 1 || data < 0 || data > 1)
		return -EINVAL;

	fade_out = (data ? true : false);

	return size;
}

static DEVICE_ATTR(led, S_IRUGO | S_IWUGO, led_status_read, led_status_write );
static DEVICE_ATTR(led_timeout, S_IRUGO | S_IWUGO, led_timeout_read, led_timeout_write );
static DEVICE_ATTR(notification_enabled, S_IRUGO | S_IWUGO, notification_enabled_read, notification_enabled_write );
static DEVICE_ATTR(notification_timeout, S_IRUGO | S_IWUGO, notification_timeout_read, notification_timeout_write );
static DEVICE_ATTR(breathing_enabled, S_IRUGO | S_IWUGO, enable_breathing_read, enable_breathing_write );
static DEVICE_ATTR(breathing_step_increment, S_IRUGO | S_IWUGO, breathing_step_incr_read, breathing_step_incr_write );
static DEVICE_ATTR(breathing_step_interval, S_IRUGO | S_IWUGO, breathing_step_int_read, breathing_step_int_write );
static DEVICE_ATTR(breathing_max_volt, S_IRUGO | S_IWUGO, breathing_max_volt_read, breathing_max_volt_write );
static DEVICE_ATTR(breathing_min_volt, S_IRUGO | S_IWUGO, breathing_min_volt_read, breathing_min_volt_write );
static DEVICE_ATTR(breathing_pause, S_IRUGO | S_IWUGO, breathing_pause_read, breathing_pause_write );
static DEVICE_ATTR(blinking_enabled, S_IRUGO | S_IWUGO, enable_blinking_read, enable_blinking_write );
static DEVICE_ATTR(blinking_int_on, S_IRUGO | S_IWUGO, blinking_int_on_read, blinking_int_on_write );
static DEVICE_ATTR(blinking_int_off, S_IRUGO | S_IWUGO, blinking_int_off_read, blinking_int_off_write );
static DEVICE_ATTR(led_fadeout, S_IRUGO | S_IWUGO, led_fadeout_read, led_fadeout_write );

static struct attribute *bl_led_attributes[] = {
	&dev_attr_led.attr,
	&dev_attr_led_timeout.attr,
	&dev_attr_notification_enabled.attr,
	&dev_attr_notification_timeout.attr,
	&dev_attr_breathing_enabled.attr,
	&dev_attr_breathing_step_increment.attr,
	&dev_attr_breathing_step_interval.attr,
	&dev_attr_breathing_max_volt.attr,
	&dev_attr_breathing_min_volt.attr,
	&dev_attr_breathing_pause.attr,
	&dev_attr_blinking_enabled.attr,
	&dev_attr_blinking_int_on.attr,
	&dev_attr_blinking_int_off.attr,
	&dev_attr_led_fadeout.attr,
	NULL
};

static struct attribute_group bln_notification_group = {
	.attrs = bl_led_attributes,
};

static struct miscdevice led_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "notification",
};

/*
 * End of the main LED Notification code block, minor ones below
 */
#endif // CONFIG_CM_BLN

#ifdef CONFIG_HAS_EARLYSUSPEND
static int sec_touchkey_early_suspend(struct early_suspend *h)
{
	struct touchkey_i2c *tkey_i2c =
		container_of(h, struct touchkey_i2c, early_suspend);
	int ret;
	int i;

	disable_irq(tkey_i2c->irq);
	ret = cancel_work_sync(&tkey_i2c->update_work);
	if (ret) {
		pr_debug("[Touchkey] enable_irq ret=%d\n", ret);
		enable_irq(tkey_i2c->irq);
	}

	/* release keys */
	for (i = 1; i < touchkey_count; ++i) {
		input_report_key(tkey_i2c->input_dev,
				 touchkey_keycode[i], 0);
	}
	input_sync(tkey_i2c->input_dev);

#ifdef CONFIG_GENERIC_BLN
	mutex_lock(&bln_sem);
	bln_enabled = true;
#endif

	touchkey_enable = 0;
	set_touchkey_debug('S');
	pr_debug("[TouchKey] sec_touchkey_early_suspend\n");
	if (touchkey_enable < 0) {
		pr_debug("[TouchKey] ---%s---touchkey_enable: %d\n",
		       __func__, touchkey_enable);
		goto out;
	}

	/* disable ldo18 */
	tkey_i2c->pdata->led_power_on(0);

	/* disable ldo11 */
	tkey_i2c->pdata->power_on(0);

out:
#ifdef CONFIG_CM_BLN
	screen_on = 0;
#endif
#ifdef CONFIG_GENERIC_BLN
	mutex_unlock(&bln_sem);
#endif
	return 0;
}

static int sec_touchkey_late_resume(struct early_suspend *h)
{
	struct touchkey_i2c *tkey_i2c =
		container_of(h, struct touchkey_i2c, early_suspend);

	set_touchkey_debug('R');
	pr_debug("[TouchKey] sec_touchkey_late_resume\n");

#ifdef CONFIG_GENERIC_BLN
	mutex_lock(&bln_sem);
#endif
#ifdef CONFIG_CM_BLN
	mutex_lock(&enable_sem);
#endif

	/* enable ldo11 */
	tkey_i2c->pdata->power_on(1);

	if (touchkey_enable < 0) {
		pr_debug("[TouchKey] ---%s---touchkey_enable: %d\n",
		       __func__, touchkey_enable);
		goto out;
	}
	msleep(50);
	tkey_i2c->pdata->led_power_on(1);

	touchkey_enable = 1;

#if defined(TK_HAS_AUTOCAL)
	touchkey_autocalibration(tkey_i2c);
#endif

#ifdef CONFIG_CM_BLN
	touch_led_control_enabled = true;
	screen_on = 1;
	/* see if late_resume is running before DISABLE_BL */
	if (led_on) {
		/* if a notification timeout was set, disable the timer */
		if (notification_timeout > 0) {
			del_timer(&notification_timer);
		}

		/* we were using a wakelock, unlock it */
		if (wake_lock_active(&led_wake_lock)) {
			wake_unlock(&led_wake_lock);
		}
		/* force DISABLE_BL to ignore the led state because we want it left on */
		led_on = 0;
	}

	if (touch_led_disabled == 0) {
		if (led_timeout != BL_ALWAYS_OFF) {
			/* ensure the light is ON */
			int data = touchkey_conv_led_data(tkey_i2c->module_ver, 1);
			change_touch_key_led_voltage(bln_tkey_i2c->brightness);
			i2c_touchkey_write(bln_tkey_i2c->client, (u8 *)&data, 1);
		}

		/* restart the timer if needed */
		if (led_timeout > 0) {
			mod_timer(&led_timer, jiffies + msecs_to_jiffies(led_timeout));
		}
	}
#else
	if (touchled_cmd_reversed) {
		touchled_cmd_reversed = 0;
		i2c_touchkey_write(tkey_i2c->client,
			(u8 *) &touchkey_led_status, 1);
		pr_debug("[Touchkey] LED returned on\n");
	}
#endif

	enable_irq(tkey_i2c->irq);

out:
#ifdef CONFIG_GENERIC_BLN
	bln_enabled = false;
	wake_unlock(&bln_wake_lock);
	mutex_unlock(&bln_sem);
#endif
#ifdef CONFIG_CM_BLN
	mutex_unlock(&enable_sem);
#endif
	return 0;
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

static int touchkey_i2c_check(struct touchkey_i2c *tkey_i2c)
{
	char data[3] = { 0, };
	int ret = 0;

	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);
	if (ret < 0) {
		pr_err("[TouchKey] module version read fail\n");
		return ret;
	}

	tkey_i2c->firmware_ver = data[1];
	tkey_i2c->module_ver = data[2];

	return ret;
}

ssize_t touchkey_update_read(struct file *filp, char *buf, size_t count,
			     loff_t *f_pos)
{
	char data[3] = { 0, };

	get_touchkey_firmware(data);
	put_user(data[1], buf);

	return 1;
}

static ssize_t touch_version_read(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	char data[3] = { 0, };
	int count;

	i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);

	count = sprintf(buf, "0x%x\n", data[1]);

	pr_debug("[TouchKey] touch_version_read 0x%x\n", data[1]);
	pr_debug("[TouchKey] module_version_read 0x%x\n", data[2]);

	return count;
}

static ssize_t touch_version_write(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	printk(KERN_DEBUG "[TouchKey] input data --> %s\n", buf);

	return size;
}

void touchkey_update_func(struct work_struct *work)
{
	struct touchkey_i2c *tkey_i2c =
		container_of(work, struct touchkey_i2c, update_work);
	int retry = 3;
#if defined(CONFIG_TARGET_LOCALE_NAATT)
	char data[3];
	i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);
	pr_debug("[Touchkey] %s: F/W version: 0x%x, Module version:0x%x\n",
	       __func__, data[1], data[2]);
#endif
	tkey_i2c->update_status = TK_UPDATE_DOWN;
	pr_debug("[Touchkey] %s: start\n", __func__);
	touchkey_enable = 0;
	while (retry--) {
		if (ISSP_main(tkey_i2c) == 0) {
			pr_debug("[TouchKey] touchkey_update succeeded\n");
			msleep(50);
			touchkey_enable = 1;
#if defined(TK_HAS_AUTOCAL)
			touchkey_autocalibration(tkey_i2c);
#endif
			tkey_i2c->update_status = TK_UPDATE_PASS;
			enable_irq(tkey_i2c->irq);
			return;
		}
		tkey_i2c->pdata->power_on(0);
	}
	enable_irq(tkey_i2c->irq);
	tkey_i2c->update_status = TK_UPDATE_FAIL;
	printk(KERN_DEBUG "[TouchKey] touchkey_update failed\n");
	return;
}

static ssize_t touch_update_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8) {
		printk(KERN_DEBUG
		       "[TouchKey] Skipping f/w update : module_version =%d\n",
		       tkey_i2c->module_ver);
		tkey_i2c->update_status = TK_UPDATE_PASS;
		return 1;
	} else {
#endif				/* CONFIG_TARGET_LOCALE_NA */
		printk(KERN_DEBUG "[TouchKey] touchkey firmware update\n");

		if (*buf == 'S') {
			disable_irq(tkey_i2c->irq);
			schedule_work(&tkey_i2c->update_work);
		}
		return size;
#ifdef CONFIG_TARGET_LOCALE_NA
	}
#endif				/* CONFIG_TARGET_LOCALE_NA */
}

static ssize_t touch_update_read(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int count = 0;

	printk(KERN_DEBUG
	       "[TouchKey] touch_update_read: update_status %d\n",
	       tkey_i2c->update_status);

	if (tkey_i2c->update_status == TK_UPDATE_PASS)
		count = sprintf(buf, "PASS\n");
	else if (tkey_i2c->update_status == TK_UPDATE_DOWN)
		count = sprintf(buf, "Downloading\n");
	else if (tkey_i2c->update_status == TK_UPDATE_FAIL)
		count = sprintf(buf, "Fail\n");

	return count;
}

static ssize_t touchkey_led_control(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int data;
	int ret;
	static const int ledCmd[] = {TK_CMD_LED_ON, TK_CMD_LED_OFF};

#if defined(CONFIG_TARGET_LOCALE_KOR)
	if (touchkey_probe == false)
		return size;
#endif
	ret = sscanf(buf, "%d", &data);
	if (ret != 1) {
		printk(KERN_DEBUG "[Touchkey] %s: %d err\n",
			__func__, __LINE__);
		return size;
	}

	if (data != 1 && data != 2) {
		printk(KERN_DEBUG "[Touchkey] %s: wrong cmd %x\n",
			__func__, data);
		return size;
	}

#if defined(CONFIG_TARGET_LOCALE_NA)
	if (tkey_i2c->module_ver >= 8)
		data = ledCmd[data-1];
#else
	data = ledCmd[data-1];
#endif

	if (touch_led_disabled == 1) {
		return size;
	}

#ifdef CONFIG_CM_BLN
	/* we have timed out or the lights should be on */
	if (led_timer.expires > jiffies || led_timeout != BL_ALWAYS_OFF) {
		int data = touchkey_conv_led_data(tkey_i2c->module_ver, 1);
		change_touch_key_led_voltage(tkey_i2c->brightness);
		ret = i2c_touchkey_write(tkey_i2c->client, (u8 *)&data, 1); /* turn on */
	}

	/* restart the timer */
	if (led_timeout > 0) {
		mod_timer(&led_timer, jiffies + msecs_to_jiffies(led_timeout));
	}
#else
	ret = i2c_touchkey_write(tkey_i2c->client, (u8 *) &data, 1);
#endif
	if (ret == -ENODEV) {
		pr_err("[Touchkey] error to write i2c\n");
		touchled_cmd_reversed = 1;
	}

    pr_debug("[Touchkey] %s: touchkey_led_status=%d\n", __func__, data);
	touchkey_led_status = data;

	return size;
}

static ssize_t touch_led_force_disable_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int ret;

    ret = sprintf(buf, "%d\n", touch_led_disabled);
    pr_info("[Touchkey] %s: touch_led_disabled=%d\n", __func__, touch_led_disabled);

    return ret;
}

static ssize_t touch_led_force_disable_store(struct device *dev,
        struct device_attribute *attr, const char *buf,
        size_t size)
{
    struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	static const int ledCmd[] = {TK_CMD_LED_ON, TK_CMD_LED_OFF};
    int data, ret;

    ret = sscanf(buf, "%d\n", &data);
    if (unlikely(ret != 1)) {
        pr_err("[Touchkey] %s: err\n", __func__);
        return -EINVAL;
    }
    pr_info("[Touchkey] %s: value=%d\n", __func__, data);
    
    if (data == 1) {
        i2c_touchkey_write(tkey_i2c->client, (u8 *) &ledCmd[1], 1);
        touchkey_led_status = TK_CMD_LED_OFF;
    }
    touch_led_disabled = data;

    return size;
}
static DEVICE_ATTR(force_disable, S_IRUGO | S_IWUSR | S_IWGRP,
        touch_led_force_disable_show, touch_led_force_disable_store);

void touchscreen_state_report(int state)
{
	// noop
}

#if defined(CONFIG_TARGET_LOCALE_NAATT) || defined(CONFIG_TARGET_LOCALE_NA)
static ssize_t touchkey_menu_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	pr_debug("[TouchKey] %s called\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8) {
		pr_debug("[Touchkey] %s: data[12] =%d,data[13] = %d\n",
		       __func__, data[12], data[13]);
		menu_sensitivity = ((0x00FF & data[12]) << 8) | data[13];
	} else {
		pr_debug("[Touchkey] %s: data[17] =%d\n", __func__,
		       data[17]);
		menu_sensitivity = data[17];
	}
#else
	pr_debug("[Touchkey] %s: data[10] =%d,data[11] = %d\n", __func__,
	       data[10], data[11]);
	menu_sensitivity = ((0x00FF & data[10]) << 8) | data[11];
#endif				/* CONFIG_TARGET_LOCALE_NA */
	return sprintf(buf, "%d\n", menu_sensitivity);
}

static ssize_t touchkey_home_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	pr_debug("[TouchKey] %s called\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8) {
		pr_debug("[Touchkey] %s: data[10] =%d,data[11] = %d\n",
		       __func__, data[10], data[11]);
		home_sensitivity = ((0x00FF & data[10]) << 8) | data[11];
	} else {
		pr_debug("[Touchkey] %s: data[15] =%d\n", __func__,
		       data[15]);
		home_sensitivity = data[15];
	}
#else
	pr_debug("[Touchkey] %s: data[12] =%d,data[13] = %d\n", __func__,
	       data[12], data[13]);
	home_sensitivity = ((0x00FF & data[12]) << 8) | data[13];
#endif				/* CONFIG_TARGET_LOCALE_NA */
	return sprintf(buf, "%d\n", home_sensitivity);
}

static ssize_t touchkey_back_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	pr_debug("[TouchKey] %s called\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8) {
		pr_debug("[Touchkey] %s: data[8] =%d,data[9] = %d\n",
		       __func__, data[8], data[9]);
		back_sensitivity = ((0x00FF & data[8]) << 8) | data[9];
	} else {
		pr_debug("[Touchkey] %s: data[13] =%d\n", __func__,
		       data[13]);
		back_sensitivity = data[13];
	}
#else
	pr_debug("[Touchkey] %s: data[14] =%d,data[15] = %d\n", __func__,
	       data[14], data[15]);
	back_sensitivity = ((0x00FF & data[14]) << 8) | data[15];
#endif				/* CONFIG_TARGET_LOCALE_NA */
	return sprintf(buf, "%d\n", back_sensitivity);
}

static ssize_t touchkey_search_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	pr_debug("[TouchKey] %s called\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);
#ifdef CONFIG_TARGET_LOCALE_NA
	if (tkey_i2c->module_ver < 8) {
		pr_debug("[Touchkey] %s: data[6] =%d,data[7] = %d\n",
		       __func__, data[6], data[7]);
		search_sensitivity = ((0x00FF & data[6]) << 8) | data[7];
	} else {
		pr_debug("[Touchkey] %s: data[11] =%d\n", __func__,
		       data[11]);
		search_sensitivity = data[11];
	}
#else
	pr_debug("[Touchkey] %s: data[16] =%d,data[17] = %d\n", __func__,
	       data[16], data[17]);
	search_sensitivity = ((0x00FF & data[16]) << 8) | data[17];
#endif				/* CONFIG_TARGET_LOCALE_NA */
	return sprintf(buf, "%d\n", search_sensitivity);
}
#else
static ssize_t touchkey_menu_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
#if defined(CONFIG_MACH_Q1_BD) \
|| (defined(CONFIG_MACH_C1) && defined(CONFIG_TARGET_LOCALE_KOR))
	u8 data[14] = { 0, };
	int ret;

	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 14);

	pr_debug("[Touchkey] %s: data[13] =%d\n", __func__, data[13]);
	menu_sensitivity = data[13];
#else
	u8 data[10];
	int ret;

	pr_debug("[TouchKey] %s called\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 10);
	menu_sensitivity = data[7];
#endif
	return sprintf(buf, "%d\n", menu_sensitivity);
}

static ssize_t touchkey_back_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
#if defined(CONFIG_MACH_Q1_BD) \
|| (defined(CONFIG_MACH_C1) && defined(CONFIG_TARGET_LOCALE_KOR))
	u8 data[14] = { 0, };
	int ret;

	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 14);

	pr_debug("[Touchkey] %s: data[11] =%d\n", __func__, data[11]);
	back_sensitivity = data[11];
#else
	u8 data[10];
	int ret;

	pr_debug("[TouchKey] %s called\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 10);
	back_sensitivity = data[9];
#endif
	return sprintf(buf, "%d\n", back_sensitivity);
}
#endif

#if defined(TK_HAS_AUTOCAL)
static ssize_t autocalibration_enable(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int data;

	sscanf(buf, "%d\n", &data);

	if (data == 1)
		touchkey_autocalibration(tkey_i2c);

	return size;
}

static ssize_t autocalibration_status(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	u8 data[6];
	int ret;
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);

	pr_debug("[Touchkey] %s\n", __func__);

	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 6);
	if ((data[5] & TK_BIT_AUTOCAL))
		return sprintf(buf, "Enabled\n");
	else
		return sprintf(buf, "Disabled\n");

}
#endif				/* CONFIG_TARGET_LOCALE_NA */

static ssize_t touch_sensitivity_control(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	unsigned char data = 0x40;
	i2c_touchkey_write(tkey_i2c->client, &data, 1);
	return size;
}

static ssize_t set_touchkey_firm_version_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return sprintf(buf, "0x%x\n", TK_FIRMWARE_VER);
}

static ssize_t set_touchkey_update_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int count = 0;
	int retry = 3;

	tkey_i2c->update_status = TK_UPDATE_DOWN;

	disable_irq(tkey_i2c->irq);

	while (retry--) {
		if (ISSP_main(tkey_i2c) == 0) {
			pr_err("[TouchKey] Touchkey_update succeeded\n");
			tkey_i2c->update_status = TK_UPDATE_PASS;
			count = 1;
			msleep(50);
			break;
		}
		pr_err("[TouchKey] touchkey_update failed... retry...\n");
	}
	if (retry <= 0) {
		/* disable ldo11 */
		tkey_i2c->pdata->power_on(0);
		count = 0;
		pr_err("[TouchKey] Touchkey_update fail\n");
		tkey_i2c->update_status = TK_UPDATE_FAIL;
		enable_irq(tkey_i2c->irq);
		return count;
	}

	enable_irq(tkey_i2c->irq);

	return count;

}

static ssize_t set_touchkey_firm_version_read_show(struct device *dev,
						   struct device_attribute
						   *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	char data[3] = { 0, };
	int count;

	i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 3);
	count = sprintf(buf, "0x%x\n", data[1]);

	pr_debug("[TouchKey] touch_version_read 0x%x\n", data[1]);
	pr_debug("[TouchKey] module_version_read 0x%x\n", data[2]);
	return count;
}

static ssize_t set_touchkey_firm_status_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int count = 0;

	pr_debug("[TouchKey] touch_update_read: update_status %d\n",
	       tkey_i2c->update_status);

	if (tkey_i2c->update_status == TK_UPDATE_PASS)
		count = sprintf(buf, "PASS\n");
	else if (tkey_i2c->update_status == TK_UPDATE_DOWN)
		count = sprintf(buf, "Downloading\n");
	else if (tkey_i2c->update_status == TK_UPDATE_FAIL)
		count = sprintf(buf, "Fail\n");

	return count;
}

#ifdef CONFIG_GENERIC_BLN
static ssize_t touchkey_bln_control(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int data, errnum;

	if (sscanf(buf, "%d\n", &data) == 1) {
		data = touchkey_conv_led_data(tkey_i2c->module_ver, data);
		errnum = i2c_touchkey_write(tkey_i2c->client, (u8 *)&data, 1);
		if (errnum == -ENODEV)
			touchled_cmd_reversed = 1;
		touchkey_led_status = data;
	} else {
		printk(KERN_ERR "[TouchKey] touchkey_bln_control Error\n");
	}

	return size;
}
#endif

#ifdef CONFIG_TWEAK_REPLACE_BACK_MENU
static ssize_t touchkey_replace_back_menu_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", replace_back_menu);
}

static ssize_t touchkey_replace_back_menu_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	sscanf(buf, "%d", &replace_back_menu);
	return size;
}
#endif


static DEVICE_ATTR(recommended_version, S_IRUGO | S_IWUSR | S_IWGRP,
		   touch_version_read, touch_version_write);
static DEVICE_ATTR(updated_version, S_IRUGO | S_IWUSR | S_IWGRP,
		   touch_update_read, touch_update_write);
static DEVICE_ATTR(brightness, S_IRUGO | S_IWUGO, NULL,
		   touchkey_led_control);
static DEVICE_ATTR(touchkey_menu, S_IRUGO | S_IWUSR | S_IWGRP,
		   touchkey_menu_show, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO | S_IWUSR | S_IWGRP,
		   touchkey_back_show, NULL);

#if defined(TK_USE_4KEY)
static DEVICE_ATTR(touchkey_home, S_IRUGO, touchkey_home_show, NULL);
static DEVICE_ATTR(touchkey_search, S_IRUGO, touchkey_search_show, NULL);
#endif

static DEVICE_ATTR(touch_sensitivity, S_IRUGO | S_IWUSR | S_IWGRP, NULL,
		   touch_sensitivity_control);
static DEVICE_ATTR(touchkey_firm_update, S_IRUGO | S_IWUSR | S_IWGRP,
	set_touchkey_update_show, NULL);
static DEVICE_ATTR(touchkey_firm_update_status, S_IRUGO | S_IWUSR | S_IWGRP,
	set_touchkey_firm_status_show, NULL);
static DEVICE_ATTR(touchkey_firm_version_phone, S_IRUGO | S_IWUSR | S_IWGRP,
	set_touchkey_firm_version_show, NULL);
static DEVICE_ATTR(touchkey_firm_version_panel, S_IRUGO | S_IWUSR | S_IWGRP,
		   set_touchkey_firm_version_read_show, NULL);
#ifdef LED_LDO_WITH_REGULATOR
static DEVICE_ATTR(touchkey_brightness, S_IRUGO | S_IWUSR | S_IWGRP,
	brightness_read, brightness_control);
#endif
#ifdef CONFIG_GENERIC_BLN
static DEVICE_ATTR(touchkey_bln_control, S_IWUGO, NULL, touchkey_bln_control);
#endif
#ifdef CONFIG_TWEAK_REPLACE_BACK_MENU
static DEVICE_ATTR(touchkey_replace_back_menu, 0666,
				touchkey_replace_back_menu_show,
				touchkey_replace_back_menu_store);
#endif

#if 0 /* #if defined(CONFIG_TARGET_LOCALE_NAATT) */
static DEVICE_ATTR(touchkey_autocal_start, S_IRUGO | S_IWUSR | S_IWGRP, NULL,
		   set_touchkey_autocal_testmode);
#endif

#if defined(TK_HAS_AUTOCAL)
static DEVICE_ATTR(touchkey_raw_data0, S_IRUGO, touchkey_raw_data0_show, NULL);
static DEVICE_ATTR(touchkey_raw_data1, S_IRUGO, touchkey_raw_data1_show, NULL);
static DEVICE_ATTR(touchkey_raw_data2, S_IRUGO, touchkey_raw_data2_show, NULL);
static DEVICE_ATTR(touchkey_raw_data3, S_IRUGO, touchkey_raw_data3_show, NULL);
static DEVICE_ATTR(touchkey_idac0, S_IRUGO, touchkey_idac0_show, NULL);
static DEVICE_ATTR(touchkey_idac1, S_IRUGO, touchkey_idac1_show, NULL);
static DEVICE_ATTR(touchkey_idac2, S_IRUGO, touchkey_idac2_show, NULL);
static DEVICE_ATTR(touchkey_idac3, S_IRUGO, touchkey_idac3_show, NULL);
static DEVICE_ATTR(touchkey_threshold, S_IRUGO, touchkey_threshold_show, NULL);
static DEVICE_ATTR(autocal_enable, S_IRUGO | S_IWUSR | S_IWGRP, NULL,
		   autocalibration_enable);
static DEVICE_ATTR(autocal_stat, S_IRUGO | S_IWUSR | S_IWGRP,
		   autocalibration_status, NULL);
#endif

static struct attribute *touchkey_attributes[] = {
	&dev_attr_recommended_version.attr,
	&dev_attr_updated_version.attr,
	&dev_attr_brightness.attr,
	&dev_attr_touchkey_menu.attr,
	&dev_attr_touchkey_back.attr,
#if defined(TK_USE_4KEY)
	&dev_attr_touchkey_home.attr,
	&dev_attr_touchkey_search.attr,
#endif
	&dev_attr_touch_sensitivity.attr,
	&dev_attr_touchkey_firm_update.attr,
	&dev_attr_touchkey_firm_update_status.attr,
	&dev_attr_touchkey_firm_version_phone.attr,
	&dev_attr_touchkey_firm_version_panel.attr,
#ifdef LED_LDO_WITH_REGULATOR
	&dev_attr_touchkey_brightness.attr,
#endif
#ifdef CONFIG_GENERIC_BLN
	&dev_attr_touchkey_bln_control.attr,
#endif
#ifdef CONFIG_TWEAK_REPLACE_BACK_MENU
	&dev_attr_touchkey_replace_back_menu.attr,
#endif
#if 0/* defined(CONFIG_TARGET_LOCALE_NAATT) */
	&dev_attr_touchkey_autocal_start.attr,
#endif
#if defined(TK_HAS_AUTOCAL)
	&dev_attr_touchkey_raw_data0.attr,
	&dev_attr_touchkey_raw_data1.attr,
	&dev_attr_touchkey_raw_data2.attr,
	&dev_attr_touchkey_raw_data3.attr,
	&dev_attr_touchkey_idac0.attr,
	&dev_attr_touchkey_idac1.attr,
	&dev_attr_touchkey_idac2.attr,
	&dev_attr_touchkey_idac3.attr,
	&dev_attr_touchkey_threshold.attr,
	&dev_attr_autocal_enable.attr,
	&dev_attr_autocal_stat.attr,
#endif
    &dev_attr_force_disable.attr,
	NULL,
};

static struct attribute_group touchkey_attr_group = {
	.attrs = touchkey_attributes,
};

static int i2c_touchkey_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct touchkey_platform_data *pdata = client->dev.platform_data;
	struct touchkey_i2c *tkey_i2c;

	struct input_dev *input_dev;
	int err = 0;
	unsigned char data;
	int i;
	int ret;

	pr_debug("[TouchKey] i2c_touchkey_probe\n");

	if (pdata == NULL) {
		printk(KERN_ERR "%s: no pdata\n", __func__);
		return -ENODEV;
	}

	/*Check I2C functionality */
	ret = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	if (ret == 0) {
		pr_err("[Touchkey] No I2C functionality found\n");
		ret = -ENODEV;
		return ret;
	}

	/*Obtain kernel memory space for touchkey i2c */
	tkey_i2c = kzalloc(sizeof(struct touchkey_i2c), GFP_KERNEL);
	if (NULL == tkey_i2c) {
		pr_err("[Touchkey] failed to allocate tkey_i2c.\n");
		return -ENOMEM;
	}

	input_dev = input_allocate_device();

	if (!input_dev) {
		pr_err("[Touchkey] failed to allocate input device\n");
		kfree(tkey_i2c);
		return -ENOMEM;
	}

	input_dev->name = "sec_touchkey";
	input_dev->phys = "sec_touchkey/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->dev.parent = &client->dev;

	/*tkey_i2c*/
	tkey_i2c->pdata = pdata;
	tkey_i2c->input_dev = input_dev;
	tkey_i2c->client = client;
	tkey_i2c->irq = client->irq;
	tkey_i2c->name = "sec_touchkey";

	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_LED, input_dev->evbit);
	set_bit(LED_MISC, input_dev->ledbit);
	set_bit(EV_KEY, input_dev->evbit);

	for (i = 1; i < touchkey_count; i++)
		set_bit(touchkey_keycode[i], input_dev->keybit);

	input_set_drvdata(input_dev, tkey_i2c);

	ret = input_register_device(input_dev);
	if (ret) {
		pr_err("[Touchkey] failed to register input device\n");
		input_free_device(input_dev);
		kfree(tkey_i2c);
		return err;
	}

	INIT_WORK(&tkey_i2c->update_work, touchkey_update_func);

	tkey_i2c->pdata->power_on(1);
	msleep(50);

	/* read key led voltage */
	get_touch_key_led_voltage(tkey_i2c);

	touchkey_enable = 1;
	data = 1;

	/*sysfs*/
	tkey_i2c->dev = device_create(sec_class, NULL, 0, NULL, "sec_touchkey");

	if (IS_ERR(tkey_i2c->dev)) {
		pr_err("[TouchKey] Failed to create device(tkey_i2c->dev)!\n");
		input_unregister_device(input_dev);
	} else {
		dev_set_drvdata(tkey_i2c->dev, tkey_i2c);
		ret = sysfs_create_group(&tkey_i2c->dev->kobj,
					&touchkey_attr_group);
		if (ret) {
			pr_err("[TouchKey]: failed to create sysfs group\n");
		}
	}

#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1)
	gpio_request(GPIO_OLED_DET, "OLED_DET");
	ret = gpio_get_value(GPIO_OLED_DET);
	pr_debug("[TouchKey] OLED_DET = %d\n", ret);

	if (ret == 0) {
		pr_debug("[TouchKey] device wasn't connected to board\n");

		input_unregister_device(input_dev);
		touchkey_probe = false;
		return -EBUSY;
	}
#else
	ret = touchkey_i2c_check(tkey_i2c);
	if (ret < 0) {
		pr_debug("[TouchKey] probe failed\n");
		input_unregister_device(input_dev);
		touchkey_probe = false;
		return -EBUSY;
	}
#endif

	ret =
		request_threaded_irq(tkey_i2c->irq, NULL, touchkey_interrupt,
				IRQF_DISABLED | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT, tkey_i2c->name, tkey_i2c);
	if (ret < 0) {
		pr_err("[Touchkey]: failed to request irq(%d) - %d\n",
			tkey_i2c->irq, ret);
		input_unregister_device(input_dev);
		touchkey_probe = false;
		return -EBUSY;
	}

	tkey_i2c->pdata->led_power_on(1);

#if defined(TK_HAS_FIRMWARE_UPDATE)
	ret = touchkey_firmware_update(tkey_i2c);
	if (ret < 0) {
		pr_err("[Touchkey]: failed firmware updating process (%d)\n",
			ret);
		input_unregister_device(input_dev);
		touchkey_probe = false;
		return -EBUSY;
	}
#endif

#ifdef CONFIG_GENERIC_BLN
	bln_tkey_i2c = tkey_i2c;
	wake_lock_init(&bln_wake_lock, WAKE_LOCK_SUSPEND, "bln_wake_lock");
	register_bln_implementation(&cypress_touchkey_bln);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	tkey_i2c->early_suspend.suspend =
		(void *)sec_touchkey_early_suspend;
	tkey_i2c->early_suspend.resume =
		(void *)sec_touchkey_late_resume;
	register_early_suspend(&tkey_i2c->early_suspend);
#endif

#if defined(TK_HAS_AUTOCAL)
	touchkey_autocalibration(tkey_i2c);
#endif

#ifdef CONFIG_CM_BLN
	err = misc_register(&led_device);
	if (err) {
		printk(KERN_ERR "[LED Notify] sysfs misc_register failed.\n");
	} else {
		if( sysfs_create_group( &led_device.this_device->kobj, &bln_notification_group) < 0){
			printk(KERN_ERR "[LED Notify] sysfs create group failed.\n");
		}
	}

	/* Setup the timer for the timeouts */
	setup_timer(&led_timer, handle_led_timeout, 0);
	setup_timer(&notification_timer, handle_notification_timeout, 0);
	setup_timer(&breathing_timer, handle_breathing_timeout, 0);

	/* wake lock for LED Notify */
	wake_lock_init(&led_wake_lock, WAKE_LOCK_SUSPEND, "led_wake_lock");

	/* turn off the LED if it is not supposed to be always on */
	if (led_timeout != BL_ALWAYS_ON) {
		int status = touchkey_conv_led_data(tkey_i2c->module_ver, 2);
		i2c_touchkey_write(tkey_i2c->client, (u8 *)&status, 1);
	}
#endif /* CONFIG_CM_BLN */

	set_touchkey_debug('K');
	return 0;
}

struct i2c_driver touchkey_i2c_driver = {
	.driver = {
		.name = "sec_touchkey_driver",
	},
	.id_table = sec_touchkey_id,
	.probe = i2c_touchkey_probe,
};

static int __init touchkey_init(void)
{
	int ret = 0;

#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1VZW) || defined(CONFIG_MACH_C2)
	if (system_rev < TOUCHKEY_FW_UPDATEABLE_HW_REV) {
		pr_debug("[Touchkey] Doesn't support this board rev %d\n",
				system_rev);
		return 0;
	}
#elif defined(CONFIG_MACH_C1)
	if (system_rev < TOUCHKEY_FW_UPDATEABLE_HW_REV) {
		pr_debug("[Touchkey] Doesn't support this board rev %d\n",
				system_rev);
		return 0;
	}
#endif

	ret = i2c_add_driver(&touchkey_i2c_driver);

	if (ret) {
		pr_err("[TouchKey] registration failed, module not inserted.ret= %d\n",
	       ret);
	}
	return ret;
}

static void __exit touchkey_exit(void)
{
	pr_debug("[TouchKey] %s\n", __func__);
	i2c_del_driver(&touchkey_i2c_driver);

#ifdef CONFIG_CM_BLN
	misc_deregister(&led_device);
	wake_lock_destroy(&led_wake_lock);
	del_timer(&led_timer);
	del_timer(&notification_timer);
	del_timer(&breathing_timer);
#endif

#ifdef CONFIG_GENERIC_BLN
	wake_lock_destroy(&bln_wake_lock);
#endif
}

late_initcall(touchkey_init);
module_exit(touchkey_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("@@@");
MODULE_DESCRIPTION("touch keypad");
