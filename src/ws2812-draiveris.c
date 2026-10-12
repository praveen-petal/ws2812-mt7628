/*
 *  ws2812-draiveris.c - Bitbang kernel driver for WS2812 NeoPixel LEDs
 *  Target: MediaTek MT7628 / MT7688 SoC (Minew G1 Gateway)
 *  Pin: GPIO 11 (24-LED ring)
 *
 *  Copyright (C) 2006-2012 OpenWrt.org
 *  Copyright (C) 2012 Žilvinas Valinskas, Saulius Lukšė
 *  Copyright (C) 2014 Jürgen Weigert <jw@owncloud.com>
 *  Copyright (C) 2016 Lukas Zeller <luz@plan44.ch>
 *  Petals Automation Adaptations for MT7628 OpenWrt 23.05 (Kernel 5.15)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/watchdog.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <asm/addrspace.h>
#include <asm/io.h>

#define LED_STRIPS_SEQUENTIAL   1
#define WS2812_MAJOR            152
#define SUCCESS                 0
#define DEVICE_NAME             "ws2812"

#ifndef KSEG1ADDR
#define KSEG1ADDR(a) (0xa0000000 | ((a) & 0x1fffffff))
#endif

#define sysRegRead(phys)         (*(volatile u32 *)KSEG1ADDR(phys))
#define sysRegWrite(phys, val)  ((*(volatile u32 *)KSEG1ADDR(phys)) = (val))

#if defined(CONFIG_ATH79)
#define SYS_REG_GPIO_OE         0x18040000L
#define SYS_REG_GPIO_IN         0x18040004L
#define SYS_REG_GPIO_OUT        0x18040008L
#define SYS_REG_GPIO_SET        0x1804000CL
#define SYS_REG_GPIO_CLEAR      0x18040010L
#define SYS_REG_RST_WATCHDOG_TIMER 0x1806000CL
#else
/* MediaTek MT7628 / MT7688 SoC GPIO Registers */
#define SYS_REG_GPIO_OE         0xb0000600L
#define SYS_REG_GPIO_SET        0xb0000630L
#define SYS_REG_GPIO_CLEAR      0xb0000640L
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Petals Automation / Saulius Lukse / Juergen Weigert");
MODULE_DESCRIPTION("WS2812 NeoPixel Bitbang Driver for MT7628 GPIO 11 (24 LEDs)");
MODULE_ALIAS_CHARDEV_MAJOR(WS2812_MAJOR);

#define GPIO_NUMBER_DEFAULT 11
static int gpio_number = 11;
module_param(gpio_number, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(gpio_number, "GPIO number for LED DIN (default 11 on Minew G1)");

static int inverted = 0;
module_param(inverted, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(inverted, "drive inverted outputs (default 0)");

static int rgbw = 0;
module_param(rgbw, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(rgbw, "RGBW LEDs SK6812 (default 0 for WS2812 RGB)");

static int gpio_count = 1;
module_param(gpio_count, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(gpio_count, "number of GPIO chains (default 1)");

static int leds_per_gpio = 24;
module_param(leds_per_gpio, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(leds_per_gpio, "number of LEDs per chain (default 24)");

static char *gpios = NULL;
module_param(gpios, charp, 0);
MODULE_PARM_DESC(gpios, "Comma separated list of GPIO numbers");

#define SET_GPIOS_H(gpio_bits)  sysRegWrite(SYS_REG_GPIO_SET, gpio_bits)
#define SET_GPIOS_L(gpio_bits)  sysRegWrite(SYS_REG_GPIO_CLEAR, gpio_bits)

#define GPIO_LIST_MAX   8
static u32 gpio_list[GPIO_LIST_MAX];
static u32 gpio_bit[GPIO_LIST_MAX];
static int gpio_bit_mask;

#define MAX_LED_BUF 1024
static char led_kbuf[MAX_LED_BUF];

void led_bits_m_inverted(u32 gpio_early_mask)
{
    SET_GPIOS_L(gpio_bit_mask);
    SET_GPIOS_L(gpio_bit_mask);
    SET_GPIOS_L(gpio_bit_mask);
    SET_GPIOS_H(gpio_early_mask);

    SET_GPIOS_H(gpio_early_mask);
    SET_GPIOS_H(gpio_early_mask);
    SET_GPIOS_H(gpio_early_mask);
    SET_GPIOS_H(gpio_early_mask);

    SET_GPIOS_H(gpio_early_mask);
    SET_GPIOS_H(gpio_early_mask);
    SET_GPIOS_H(gpio_early_mask);

    SET_GPIOS_H(gpio_bit_mask);
    SET_GPIOS_H(gpio_bit_mask);
    SET_GPIOS_H(gpio_bit_mask);
    SET_GPIOS_H(gpio_bit_mask);
}

void led_bits_m(u32 gpio_early_mask)
{
    SET_GPIOS_H(gpio_bit_mask);
    SET_GPIOS_H(gpio_bit_mask);
    SET_GPIOS_H(gpio_bit_mask);
    SET_GPIOS_L(gpio_early_mask);

    SET_GPIOS_L(gpio_early_mask);
    SET_GPIOS_L(gpio_early_mask);
    SET_GPIOS_L(gpio_early_mask);
    SET_GPIOS_L(gpio_early_mask);

    SET_GPIOS_L(gpio_early_mask);
    SET_GPIOS_L(gpio_early_mask);
    SET_GPIOS_L(gpio_early_mask);
    SET_GPIOS_L(gpio_early_mask);

    SET_GPIOS_L(gpio_bit_mask);
    SET_GPIOS_L(gpio_bit_mask);
    SET_GPIOS_L(gpio_bit_mask);
    SET_GPIOS_L(gpio_bit_mask);
}

void update_leds(const char *buff, size_t len)
{
  unsigned long flags;
  long int i = 0;
  int gpio_used = gpio_count;
  int stride = 0;
  static int grb_increment[3+1] = { -1, 2, 2, /* start */ 1 };
  static int grbw_increment[4+1] = { -1, 2, 1, 2, /* start */ 1 };
  int component_idx = 0;
  int numComponents = 0;
  int *incrementTable = NULL;

  static DEFINE_SPINLOCK(critical);

  if (rgbw) {
    numComponents = 4;
    incrementTable = grbw_increment;
  }
  else {
    numComponents = 3;
    incrementTable = grb_increment;
  }
  stride = numComponents * leds_per_gpio;

  if (stride > len) stride = len;
  if (len < gpio_used * stride) gpio_used = (int)(len/stride)+1;

#if defined(CONFIG_ATH79)
  sysRegWrite(SYS_REG_RST_WATCHDOG_TIMER, 1<<31);
#endif

  i = sysRegRead(SYS_REG_GPIO_OE);
  sysRegWrite(SYS_REG_GPIO_OE, i | gpio_bit_mask);

  if (inverted) {
      for(i=0; i<1000; i++) {
        SET_GPIOS_H(gpio_bit_mask);
      }

      spin_lock_irqsave(&critical, flags);
      for (i = incrementTable[numComponents]; i < stride; i += incrementTable[component_idx++])
      {
#if LED_STRIPS_SEQUENTIAL
        int color_bit;
        int color_bit_mask = 0x80;

        for (color_bit = 8; color_bit > 0; color_bit--)
          {
            u32 low_gpios = 0;
            int idx;
            int pos = i;
            for (idx = 0; idx < gpio_used; idx++)
              {
                if (pos >= len || !(buff[pos] & color_bit_mask)) low_gpios |= gpio_bit[idx];
                pos += stride;
              }
            led_bits_m_inverted(low_gpios);
            color_bit_mask >>= 1;
          }
#else
        unsigned char c = buff[i];
        if (c & 0x80) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
        if (c & 0x40) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
        if (c & 0x20) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
        if (c & 0x10) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
        if (c & 0x08) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
        if (c & 0x04) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
        if (c & 0x02) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
        if (c & 0x01) led_bits_m_inverted(0); else led_bits_m_inverted(gpio_bit_mask);
#endif
        if (component_idx >= numComponents) component_idx = 0;
      }
      spin_unlock_irqrestore(&critical, flags);
  }
  else {
      for(i=0; i<1000; i++) {
        SET_GPIOS_L(gpio_bit_mask);
      }

      spin_lock_irqsave(&critical, flags);
      for (i = incrementTable[numComponents]; i < stride; i += incrementTable[component_idx++])
      {
#if LED_STRIPS_SEQUENTIAL
        int color_bit;
        int color_bit_mask = 0x80;

        for (color_bit = 8; color_bit > 0; color_bit--)
          {
            u32 low_gpios = 0;
            int idx;
            int pos = i;
            for (idx = 0; idx < gpio_used; idx++)
              {
                if (pos >= len || !(buff[pos] & color_bit_mask)) low_gpios |= gpio_bit[idx];
                pos += stride;
              }
            led_bits_m(low_gpios);
            color_bit_mask >>= 1;
          }
#else
        unsigned char c = buff[i];
        if (c & 0x80) led_bits_m(0); else led_bits_m(gpio_bit_mask);
        if (c & 0x40) led_bits_m(0); else led_bits_m(gpio_bit_mask);
        if (c & 0x20) led_bits_m(0); else led_bits_m(gpio_bit_mask);
        if (c & 0x10) led_bits_m(0); else led_bits_m(gpio_bit_mask);
        if (c & 0x08) led_bits_m(0); else led_bits_m(gpio_bit_mask);
        if (c & 0x04) led_bits_m(0); else led_bits_m(gpio_bit_mask);
        if (c & 0x02) led_bits_m(0); else led_bits_m(gpio_bit_mask);
        if (c & 0x01) led_bits_m(0); else led_bits_m(gpio_bit_mask);
#endif
        if (component_idx >= numComponents) component_idx = 0;
      }
      spin_unlock_irqrestore(&critical, flags);
  }
}

static int Device_Open = 0;
#define BUF_LEN 100
static char msg[BUF_LEN];
static char *msg_Ptr;

static int device_open(struct inode *inode, struct file *file)
{
  static int counter = 0;

  if (Device_Open)
    return -EBUSY;

  Device_Open++;
  snprintf(msg, sizeof(msg), "Petals WS2812 Controller open count: %d\n", counter++);
  msg_Ptr = msg;
  try_module_get(THIS_MODULE);

  return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file)
{
  Device_Open--;
  module_put(THIS_MODULE);
  return 0;
}

static ssize_t device_read(struct file *filp, char __user *buffer, size_t length, loff_t *offset)
{
  int bytes_read = 0;

  if (*msg_Ptr == 0)
    return 0;

  while (length && *msg_Ptr) {
    if (put_user(*(msg_Ptr++), buffer++))
      return -EFAULT;
    length--;
    bytes_read++;
  }

  return bytes_read;
}

static ssize_t device_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
  size_t count = len;
  if (count > MAX_LED_BUF)
    count = MAX_LED_BUF;

  if (copy_from_user(led_kbuf, buff, count))
    return -EFAULT;

  update_leds(led_kbuf, count);
  return count;
}

static struct file_operations fops = {
  .owner   = THIS_MODULE,
  .read    = device_read,
  .write   = device_write,
  .open    = device_open,
  .release = device_release
};

static struct class *ws2812_class = NULL;

static int __init ws2812_init(void)
{
  int r;

  r = register_chrdev(WS2812_MAJOR, DEVICE_NAME, &fops);
  if (r < 0) {
    printk(KERN_ALERT DEVICE_NAME": Registering char device major=%d failed with %d\n", WS2812_MAJOR, r);
    return r;
  }

  if (gpio_count > GPIO_LIST_MAX) {
    printk(KERN_ALERT DEVICE_NAME": Error: gpio_count=%d > MAX=%d\n", gpio_count, GPIO_LIST_MAX);
    unregister_chrdev(WS2812_MAJOR, DEVICE_NAME);
    return -EINVAL;
  }

  if (gpios && gpio_number >= 0) {
    printk(KERN_ALERT DEVICE_NAME": Error: specify either gpios='%s' or gpio_number=%d, not both\n", gpios, gpio_number);
    unregister_chrdev(WS2812_MAJOR, DEVICE_NAME);
    return -EINVAL;
  }

  gpio_bit_mask = 0;

  if (gpios) {
    int idx = 0;
    char *p = gpios;
    while (*p) {
      int n = 0;
      while (*p && (*p < '0' || *p > '9')) p++;
      if (!*p) break;
      while (*p >= '0' && *p <= '9') n = 10*n + *p++ - '0';
      gpio_list[idx] = n;
      gpio_bit[idx]  = 1<<n;
      gpio_bit_mask |= 1<<n;
      idx++;
    }
    gpio_number = gpio_list[0];
    gpio_count = idx;
    if (!gpio_count) {
      printk(KERN_ALERT DEVICE_NAME": Error: cannot parse gpios='%s'\n", gpios);
      unregister_chrdev(WS2812_MAJOR, DEVICE_NAME);
      return -EINVAL;
    }
  } else {
    int idx;
    if (gpio_number < 0) gpio_number = GPIO_NUMBER_DEFAULT;
    for (idx = gpio_number; idx < gpio_number+gpio_count; idx++) {
      gpio_list[idx-gpio_number] = idx;
      gpio_bit[idx-gpio_number] = 1<<idx;
      gpio_bit_mask |= 1<<idx;
    }
  }

  printk(KERN_INFO DEVICE_NAME": Major=%d device=/dev/%s\n", WS2812_MAJOR, DEVICE_NAME);
  printk(KERN_INFO DEVICE_NAME": Base GPIO: %d, count: %d, leds_per_gpio: %d\n", gpio_number, gpio_count, leds_per_gpio);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
  ws2812_class = class_create(DEVICE_NAME);
#else
  ws2812_class = class_create(THIS_MODULE, DEVICE_NAME);
#endif
  if (IS_ERR(ws2812_class)) {
    printk(KERN_ALERT DEVICE_NAME": class_create failed with %ld\n", PTR_ERR(ws2812_class));
    unregister_chrdev(WS2812_MAJOR, DEVICE_NAME);
    return PTR_ERR(ws2812_class);
  }

  if (!device_create(ws2812_class, NULL, MKDEV(WS2812_MAJOR, 0), NULL, DEVICE_NAME)) {
    printk(KERN_ALERT DEVICE_NAME": device_create failed. Will need mknod /dev/%s c %d 0\n", DEVICE_NAME, WS2812_MAJOR);
  }

  return SUCCESS;
}

static void __exit ws2812_exit(void)
{
  if (ws2812_class) {
    device_destroy(ws2812_class, MKDEV(WS2812_MAJOR, 0));
    class_destroy(ws2812_class);
  }
  unregister_chrdev(WS2812_MAJOR, DEVICE_NAME);
  printk(KERN_INFO DEVICE_NAME": Driver unloaded.\n");
}

module_init(ws2812_init);
module_exit(ws2812_exit);
