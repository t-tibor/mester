#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>

#include <linux/miscdevice.h>


// char dev interface
static struct miscdevice misc;
static struct file_operations dev_fops;



MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBon timers.");