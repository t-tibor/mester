#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/uaccess.h>

#include <linux/miscdevice.h>
#include <asm/ioctl.h>
#include <linux/sysfs.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/circ_buf.h>
#include <linux/clk.h>
#include <linux/interrupt.h>

#include <linux/pm_runtime.h>

#include "../event_mux/event_mux.h"


// DMTimer register offsets
#define TIMER_IRQSTATUS_RAW_OFFSET	0x24
#define TIMER_IRQSTATUS_OFFSET 		0x28
#define 	TCAR_IT_FLAG			(1<<2)
#define 	OVF_IT_FLAG				(1<<1)
#define 	MAT_IT_FLAG				(1<<0)
#define TIMER_IRQENABLE_SET_OFFSET	0x2C
#define TIMER_IRQENABLE_CLR_OFFSET	0x30

#define TIMER_TCAR1_OFFSET			0x50
#define TIMER_TCAR2_OFFSET			0x58

/*
TODO:
	check timer clock source

*/

// type declarations

struct circular_buffer
{
	uint32_t *buffer;
	uint32_t head;
	uint32_t tail;
	uint32_t length;
	int ovf;
};

struct DMTimer_priv
{
	char *name;
	uint32_t idx;
	int irq;
	struct clk *fclk;

	void __iomem *io_base;				// kernel virtual address
	unsigned long regspace_phys_base;	// physical base address
	uint32_t regspace_size;				// memory region size

	struct miscdevice misc;
	struct platform_device *pdev;

	struct circular_buffer circ_buf;
};


// <------------------- CLOCK SOURCE SETTINGS ------------------->
#define CLK_DISABLE		0x00
#define CLK_ENABLE		0x01

static int timer_set_clk_state(struct DMTimer_priv *timer, u32 arg)
{
	int ret = 0;

	if(unlikely(arg > 1) || unlikely(!timer) || IS_ERR(timer->fclk))
		return -EINVAL;

	switch(arg)
	{
		case CLK_DISABLE:
			clk_disable(timer->fclk);
			break;
		case CLK_ENABLE:
			ret = clk_enable(timer->fclk);
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

#define CLOCK_SOURCE_SYSCLK		0x01
#define	CLOCK_SOURCE_TCLKIN		0x02
static int timer_set_clksource(struct DMTimer_priv *timer, uint32_t source)
{
	const char *parent_name;
	struct clk *parent;
	int parent_num  = 0;
	int ret;

	if (unlikely(!timer) || IS_ERR(timer->fclk))
			return -EINVAL;

	switch(source)
	{
		case CLOCK_SOURCE_SYSCLK:
			parent_name = "sys_clkin_ck";
			break;
		case CLOCK_SOURCE_TCLKIN:
			parent_name = "tclkin_ck";
			break;
		default:
			dev_err(&timer->pdev->dev,"%s:%d:Invalid clock source to select: %d.\n",__FUNCTION__,__LINE__,source);
			return -EINVAL;
			break;
	}


	if ((parent_num = clk_hw_get_num_parents(__clk_get_hw(timer->fclk))) < 2)
	{
		dev_err(&timer->pdev->dev,"[%s:%d] Selected timer has only %d parents (expected at least 2).\n",__FUNCTION__,__LINE__,parent_num);
		return -EINVAL;
	}


	parent = clk_get(&timer->pdev->dev, parent_name);
	if (IS_ERR(parent)) {
		dev_err(&timer->pdev->dev,"[%s:%d] %s not found\n", __FUNCTION__,__LINE__, parent_name);
		return -EINVAL;
	}

	ret = clk_set_parent(timer->fclk, parent);
	if (ret < 0)
		dev_err(&timer->pdev->dev,"[%s:%d] Failed to set %s as parent\n", __FUNCTION__,__LINE__,parent_name);

	clk_put(parent);

	return ret;
}

// <------------------------  Timer character device operations ------------------------>

static ssize_t timer_cdev_open(struct inode *inode, struct file *pfile)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static int timer_cdev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
	return 0;
}



static int timer_cdev_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (filep->private_data);
	struct DMTimer_priv *timer= container_of(misc_dev, struct DMTimer_priv, misc);

	// the offset is not used
	unsigned long physical = timer->regspace_phys_base;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long psize = timer->regspace_size;

	if (vsize > psize)
	    return -EINVAL; /*  spans too high */

	remap_pfn_range(vma, vma->vm_start, physical, vsize, vma->vm_page_prot);
	return 0;
}

#define IOCTL_SET_CLOCK_STATE	0x20
#define IOCTL_SET_CLOCK_SOURCE 	0x21

#define IOCTL_SET_ICAP_SOURCE	0x30

static long timer_cdev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct DMTimer_priv *timer = container_of(misc_dev, struct DMTimer_priv, misc);
	int ret = 0;

	switch(cmd)
	{
		case IOCTL_SET_CLOCK_STATE:
			ret = timer_set_clk_state(timer,arg);
			break;
		case IOCTL_SET_CLOCK_SOURCE:
			ret = timer_set_clksource(timer,arg);
			break;
			
		case IOCTL_SET_ICAP_SOURCE:
			ret = event_mux_set_dmtimer_event(timer->idx,arg);
			break;

		default:
			dev_err(&timer->pdev->dev,"Invalid IOCTL command : %d.\n",cmd);
			break;
	}
	return ret;
}

static ssize_t timer_cdev_read (struct file *pfile, char __user *buff, size_t len, loff_t *ppos)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct DMTimer_priv * timer = container_of(misc_dev, struct DMTimer_priv, misc);
	struct circular_buffer *circ = &timer->circ_buf;
	// get data count from the circular buffer

	uint32_t head = smp_load_acquire(&circ->head);
	uint32_t tail = circ->tail;
	uint32_t data_cnt = CIRC_CNT(head,tail,circ->length);
	uint32_t buff_capa = len/4;
	uint32_t data_to_read = min(data_cnt, buff_capa);
	uint32_t data_to_end = CIRC_CNT_TO_END(head,tail,circ->length);
	uint32_t step1_length = min(data_to_read,data_to_end);
	uint32_t step2_length = data_to_read - step1_length;


	if (data_to_read >= 1) 
	{
		// worst case we need to copy the data in 2 steps, if the data block is at the end of the buffer
		if(copy_to_user(buff,&circ->buffer[tail],step1_length*sizeof(uint32_t)))
			return -EFAULT;

		smp_store_release(&circ->tail,
				  (tail + step1_length) & (circ->length - 1));

		if(step2_length > 0)
		{
			if(copy_to_user(buff+step1_length*sizeof(uint32_t),&circ->buffer[tail],step2_length*sizeof(uint32_t)))
				return -EFAULT;

			smp_store_release(&circ->tail,
				  (tail + step2_length) & (circ->length - 1));
		}
	}

	return data_to_read*sizeof(uint32_t);

}

static struct file_operations timer_cdev_fops = 
{
	.open = timer_cdev_open,
	.mmap = timer_cdev_mmap,
	.release = timer_cdev_close,
	.read = timer_cdev_read,
	.unlocked_ioctl = timer_cdev_ioctl
};

// <------------------------  SYSFS INTERFACE ------------------------>

static ssize_t timer_data_cnt_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	u32 head, tail, size;
    struct DMTimer_priv *timer = dev_get_drvdata(dev);
    int len;

	head = READ_ONCE(timer->circ_buf.head);
	tail = READ_ONCE(timer->circ_buf.tail);
	size = CIRC_CNT(head,tail,timer->circ_buf.length);

    len = sprintf(buf, "%d\n", size);
    if (len <= 0)
        dev_err(dev, "[%s:%d]: Invalid sprintf len: %d\n", __FUNCTION__,__LINE__,len);

    return len;
}
static DEVICE_ATTR(data_count, S_IRUGO, timer_data_cnt_show,NULL);

static ssize_t timer_ovf_cnt_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct DMTimer_priv *timer = dev_get_drvdata(dev);
    int len;

    len = sprintf(buf, "%d\n", timer->circ_buf.ovf);
    if (len <= 0)
        dev_err(dev, "[%s:%d]: Invalid sprintf len: %d\n", __FUNCTION__,__LINE__,len);

    return len;
}
static DEVICE_ATTR(ovf_count, S_IRUGO, timer_ovf_cnt_show,NULL);

static struct attribute *buffer_attrs[] = {
    &dev_attr_data_count.attr,
    &dev_attr_ovf_count.attr,
    NULL
};

static struct attribute_group buffer_group = {
    .name = "buffer",
    .attrs = buffer_attrs,
};



static irqreturn_t timer_irq_handler(int irq, void *data)
{
	struct DMTimer_priv *timer= (struct DMTimer_priv*)data;
	// clear interrupt line and save the input capture flag
	uint32_t irq_status;
	uint32_t icap;
	uint32_t head,tail;

	irq_status = readl_relaxed(timer->io_base + TIMER_IRQSTATUS_OFFSET);
	// clear interrupt flag
	iowrite32(irq_status,timer->io_base + TIMER_IRQSTATUS_OFFSET);

	if(irq_status & TCAR_IT_FLAG)
	{
		icap = ioread32(timer->io_base + TIMER_TCAR1_OFFSET);
		// put input capture data to the circular buffer

		head = timer->circ_buf.head;
		tail = READ_ONCE(timer->circ_buf.tail);

		if (CIRC_SPACE(timer->circ_buf.head, timer->circ_buf.tail, timer->circ_buf.length) >= 1) 
		{
			/* insert one item into the buffer */
			timer->circ_buf.buffer[head] = icap;

			smp_store_release(&timer->circ_buf.head,
					  (head + 1) & (timer->circ_buf.length - 1));

		}
		else
		{
			timer->circ_buf.ovf++;
		}
	}
	return IRQ_HANDLED;
}


static int timer_probe(struct platform_device *pdev)
{
	const char *timer_name;
	int timer_idx;
	struct resource *mem, *irq;
	struct DMTimer_priv *timer;
	int ret;
	char *irq_name;

	// read up timer index
	of_property_read_string_index(pdev->dev.of_node, "ti,hwmods", 0, &timer_name);
	sscanf(timer_name, "timer%d", &timer_idx);
	if(timer_idx < 4 || timer_idx > 7) 
	{
		pr_err("Invalid timer index found in the device tree: %d\n",timer_idx);
		return -EINVAL;
	}


	// timer is valid, so lets get its resources
	irq = platform_get_resource(pdev,IORESOURCE_IRQ,0);
	if(unlikely( NULL  ==  irq))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get interrupt.\n",__func__,__LINE__);
		return -ENODEV;
	}
	mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(unlikely(NULL == mem))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get timer memory.\n",__func__,__LINE__);
		return -ENODEV;
	}

	// alloc private descriptor struct
	timer = devm_kzalloc(&pdev->dev,sizeof(struct DMTimer_priv),GFP_KERNEL);
	if(!timer)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot allocate memory.\n",__func__,__LINE__);
		return -ENOMEM;
	}
	timer->pdev = pdev;
	timer->idx = timer_idx;
	timer->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "DMTimer%d",timer_idx);

	// remapping registers
	timer->io_base =  devm_ioremap_resource(&pdev->dev, mem);
 	if (IS_ERR(timer->io_base))
 	{
 		dev_err(&pdev->dev,"%s,%d: Cannot remap the timer memory range.\n",__func__,__LINE__);
 		return PTR_ERR(timer->io_base);
 	}
 	timer->regspace_size = resource_size(mem);
 	timer->regspace_phys_base = mem->start;

 	// remapping irq
 	timer->irq = irq->start;
 	irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_irq",
					  dev_name(&pdev->dev),timer_idx);
 	ret = devm_request_irq(&pdev->dev,timer->irq,timer_irq_handler,0,irq_name,timer);
 	if(unlikely(ret))
 	{
 		dev_err(&pdev->dev, "%s:%d Interrupt allocation (irq:%d) failed [%d].\n", __func__,__LINE__,timer->irq,ret);
		return -ENXIO;
 	}


 	// get timer clock
 	timer->fclk = devm_clk_get(&pdev->dev,"fck");
 	if(IS_ERR(timer->fclk))
 	{
 		dev_err(&pdev->dev,"%s:%d Cannot get clock for the timer.\n",__FUNCTION__,__LINE__);
 		return -ENODEV;
 	}

 	// init circular buffer
 	timer->circ_buf.head = timer->circ_buf.tail = timer->circ_buf.ovf = 0;
 	timer->circ_buf.length = 256;
 	timer->circ_buf.buffer = devm_kzalloc(&pdev->dev,timer->circ_buf.length * sizeof(uint32_t), GFP_KERNEL);


 	// setup misc character device
 	timer->misc.fops = &timer_cdev_fops;
	timer->misc.minor = MISC_DYNAMIC_MINOR;
	timer->misc.name =timer->name;
	if(misc_register(&timer->misc))
	{
		dev_err(&pdev->dev,"Couldn't initialize miscdevice /dev/%s.\n",timer->misc.name);
		return -ENODEV;
	}


	dev_info(&pdev->dev,"%s initialization succeeded.\n",timer->name);
	dev_info(&pdev->dev,"Base address: 0x%lx, length: %u, irq num: %d.\n",timer->regspace_phys_base,timer->regspace_size,timer->irq);

 	platform_set_drvdata(pdev,timer);

 	// set sysfs attributes
 	if(sysfs_create_group(&pdev->dev.kobj, &buffer_group))
 	{
 		dev_err(&pdev->dev,"[%s:%d] Cannot create sysfs attributes.\n",__FUNCTION__,__LINE__);
 	}


 	// start the timer interface clock
 	pm_runtime_enable(&pdev->dev);
 	if(pm_runtime_get_sync(&pdev->dev) < 0)
 	{
 		dev_err(&pdev->dev,"[%s:%d] Cannot resume the timer.\n",__FUNCTION__,__LINE__);
 	}

 	return 0;

}

static int timer_remove(struct platform_device *pdev)
{
	struct DMTimer_priv *priv;
	priv = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &buffer_group);

	// stop the timer interface clock
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	misc_deregister(&priv->misc);

	// all other resourcees are freed managed
	return 0;
}				





static struct of_device_id timer_of_match[] = {
	{ .compatible = "ti,am335x-timer", },
	{ }
};



static struct platform_driver timer_platform_driver = {
	.probe = timer_probe,
	.remove = timer_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "RTIO_DMTimer",
		.of_match_table = timer_of_match,
	},
};


module_platform_driver(timer_platform_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBone timers.");