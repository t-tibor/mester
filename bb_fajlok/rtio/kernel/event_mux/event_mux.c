#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/of_address.h>

#include <linux/miscdevice.h>

#include "./event_mux.h"



///////////////////////////////////////////////
// Control module registers
///////////////////////////////////////////////
#define CONTROL_MODULE_BASE 	0x44E10000
#define TIMER_EVT_CAPT_OFFSET	0xFD0
#define ECAP_EVT_CAPT_OFFSET	0xFD4
#define ADC_EVT_CAPT_OFFSET		0xFD8

#define CM_SECTION_START		((CONTROL_MODULE_BASE) + (TIMER_EVT_CAPT_OFFSET))
#define CM_SECTION_LENGTH		12			// 12 byte region

volatile void __iomem *timer_evt_capt_reg = NULL;
volatile void __iomem *ecap_evt_capt_reg = NULL;
volatile void __iomem *adc_evt_capt_reg = NULL;

static struct kobject *event_mux_kobj;

// <------------------------------ KERNEL INTERFACE ------------------------------>

int event_mux_set_dmtimer_event(u32 timer_idx, u32 event_id)
{
	u32 reg_val;
	int sh;

	if(timer_idx < 5 || timer_idx > 7)
		return -EINVAL;

	if(event_id > 30) 
		return -EINVAL;

	sh = (timer_idx-5)*8;

	reg_val = ioread32(timer_evt_capt_reg);
	reg_val &= ~(0xFF << sh);
	reg_val |= (event_id << sh);

	iowrite32(reg_val, timer_evt_capt_reg);
	return 0;
}
EXPORT_SYMBOL_GPL(event_mux_set_dmtimer_event);

int event_mux_get_dmtimer_event(u32 timer_idx)
{
	u32 reg_val;
	int sh;

	if(timer_idx < 5 || timer_idx > 7)
		return -EINVAL;


	sh = (timer_idx-5)*8;

	reg_val = ioread32(timer_evt_capt_reg);
	reg_val >>= sh;
	reg_val &= 0xFF;

	return reg_val;
}
EXPORT_SYMBOL_GPL(event_mux_get_dmtimer_event);

int event_mux_set_ecap_event(u32 ecap_idx, u32 event_id)
{
	u32 reg_val;
	int sh;

	if(ecap_idx > 2)
		return -EINVAL;

	if(event_id > 30) 
		return -EINVAL;

	sh = (ecap_idx)*8;

	reg_val = ioread32(ecap_evt_capt_reg);
	reg_val &= ~(0xFF << sh);
	reg_val |= (event_id << sh);

	iowrite32(reg_val, ecap_evt_capt_reg);
	return 0;
}
EXPORT_SYMBOL_GPL(event_mux_set_ecap_event);

int event_mux_get_ecap_event(u32 ecap_idx)
{
	u32 reg_val;
	int sh;

	if(ecap_idx > 2)
		return -EINVAL;


	sh = (ecap_idx)*8;

	reg_val = ioread32(ecap_evt_capt_reg);
	reg_val >>= sh;
	reg_val &= 0xFF;
	return reg_val;
}
EXPORT_SYMBOL_GPL(event_mux_get_ecap_event);


int event_mux_set_adc_event(u32 event_id)
{
	if(event_id > 4) 
		return -EINVAL;
	iowrite32(event_id, adc_evt_capt_reg);
	return 0;
}
EXPORT_SYMBOL_GPL(event_mux_set_adc_event);

int event_mux_get_adc_event(void)
{
	return ioread32(adc_evt_capt_reg);
}
EXPORT_SYMBOL_GPL(event_mux_get_adc_event);


// <------------------ SYSFS INTERFACE ----------------------->
// timer 5
static ssize_t dmtimer5_event_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
        int event = event_mux_get_dmtimer_event(5);
        return sprintf(buf,"%d\n",event);
}

static ssize_t dmtimer5_event_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
		u32 event;
        sscanf(buf, "%u", &event);
        event_mux_set_dmtimer_event(5,event);
        return count;
}

static struct kobj_attribute dmtimer5_attribute =__ATTR(dmtimer5_event, 0660, dmtimer5_event_show,
                                                   dmtimer5_event_store);

// timer 6
static ssize_t dmtimer6_event_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
        int event = event_mux_get_dmtimer_event(6);
        return sprintf(buf,"%d\n",event);
}

static ssize_t dmtimer6_event_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
		u32 event;
        sscanf(buf, "%u", &event);
        event_mux_set_dmtimer_event(6,event);
        return count;
}

static struct kobj_attribute dmtimer6_attribute =__ATTR(dmtimer6_event, 0660, dmtimer6_event_show,
                                                   dmtimer6_event_store);




// timer 7
static ssize_t dmtimer7_event_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
        int event = event_mux_get_dmtimer_event(7);
        return sprintf(buf,"%d\n",event);
}

static ssize_t dmtimer7_event_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
		u32 event;
        sscanf(buf, "%u", &event);
        event_mux_set_dmtimer_event(7,event);
        return count;
}

static struct kobj_attribute dmtimer7_attribute =__ATTR(dmtimer7_event, 0660, dmtimer7_event_show,
                                                   dmtimer7_event_store);




// ecap0
static ssize_t ecap0_event_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
        int event = event_mux_get_ecap_event(0);
        return sprintf(buf,"%d\n",event);
}

static ssize_t ecap0_event_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
		u32 event;
        sscanf(buf, "%u", &event);
        event_mux_set_ecap_event(0,event);
        return count;
}

static struct kobj_attribute ecap0_attribute =__ATTR(ecap0_event, 0660, ecap0_event_show,
                                                   ecap0_event_store);

// ecap1
static ssize_t ecap1_event_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
        int event = event_mux_get_ecap_event(1);
        return sprintf(buf,"%d\n",event);
}

static ssize_t ecap1_event_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
		u32 event;
        sscanf(buf, "%u", &event);
        event_mux_set_ecap_event(1,event);
        return count;
}

static struct kobj_attribute ecap1_attribute =__ATTR(ecap1_event, 0660, ecap1_event_show,
                                                   ecap1_event_store);

// ecap2
static ssize_t ecap2_event_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
        int event = event_mux_get_ecap_event(2);
        return sprintf(buf,"%d\n",event);
}

static ssize_t ecap2_event_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
		u32 event;
        sscanf(buf, "%u", &event);
        event_mux_set_ecap_event(2,event);
        return count;
}

static struct kobj_attribute ecap2_attribute =__ATTR(ecap2_event, 0660, ecap2_event_show,
                                                   ecap2_event_store);


static int __init event_mux_init(void)
{

	// alloc memory region in the Control module memory space
	if(request_mem_region(CM_SECTION_START, CM_SECTION_LENGTH, "Control module dmtimer, ecap, adc event mux") == NULL)
	{
		pr_err("Cannot request the memory region.");
		goto err;
	}

	timer_evt_capt_reg = ioremap(CM_SECTION_START, CM_SECTION_LENGTH);
	if(!timer_evt_capt_reg)
	{
		pr_err("Cannot remap the memroy region.");
		goto err1;
	}
	ecap_evt_capt_reg = timer_evt_capt_reg + 4;
	adc_evt_capt_reg = timer_evt_capt_reg + 8;
	

	pr_info("Event mux memory region remapped.");

	event_mux_kobj = kobject_create_and_add("event_mux",kernel_kobj);
	if(!event_mux_kobj)
	{
		pr_err("Failed to create evet_mux kobject\n");
		goto err2;
	}


        if (sysfs_create_file(event_mux_kobj, &dmtimer5_attribute.attr) ||
        	sysfs_create_file(event_mux_kobj, &dmtimer6_attribute.attr) ||
        	sysfs_create_file(event_mux_kobj, &dmtimer7_attribute.attr) ||
        	sysfs_create_file(event_mux_kobj, &ecap0_attribute.attr) ||
        	sysfs_create_file(event_mux_kobj, &ecap1_attribute.attr) ||
        	sysfs_create_file(event_mux_kobj, &ecap2_attribute.attr)) {
                pr_err("Failed to create an attribute file\n");
        }



	return 0;
err2:
	iounmap(timer_evt_capt_reg);
err1:
	release_mem_region(CM_SECTION_START, CM_SECTION_LENGTH);
err:
	return -ENODEV;
}

static void __exit event_mux_exit(void)
{
	kobject_put(event_mux_kobj);
	if(timer_evt_capt_reg)
	{
		iounmap(timer_evt_capt_reg);
		release_mem_region(CM_SECTION_START, CM_SECTION_LENGTH);
	}
}

module_init(event_mux_init);
module_exit(event_mux_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBone timers.");