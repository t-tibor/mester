#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>

#include <linux/miscdevice.h>
#include <asm/ioctl.h>

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


// type declarations

struct circular_buffer
{
	uint32_t *buffer;
	uint32_t head;
	uint32_t tail;
	uint32_t length;
	int ovf;
}

static struct DMTimer_priv
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


// Timer character device operations

static ssize_t timer_cdev_open(struct inode *inode, struct file *pfile)
{
	try_module_get(THIS_MODULE);
}

static int timer_cdev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
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

	remap_pfn_range(vma, vma_>vm_start, physical, vsize, vma->vm_page_prot);
	return 0;
}

#define IOCTL_GET_OVF	0x00

static long timer_cdev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	struct DMTimer_priv *timer = (struct DMTimer_priv*) (pfile->private_data);

	switch(cmd)
	{
		case IOCTL_GET_OVF:
			return timer->circ_buf.ovf;
		break;

		case IOCTL_GET_DATA_CNT:
			{
				uint32_t head, tail;
				head = READ_ONCE(timer->circ_buf.head);
				tail = READ_ONCE(timer->circ_buf.tail);
				return CIRC_CNT(head,tail,timer->circ_buf.length);
			}
			break;
	}
	// get ovgf counter
	// get circ buffer data count


}

static ssize_t timer_cdev_read (struct file *pfile, char __user *buff, size_t len, loff_t *ppos)
{
	struct DMTimer_priv * timer = (struct DMTimer_priv*)(pfile->private_data);
	struct circular_buffer *circ = &timer->circ_buf;
	// get data count from the circular buffer

	uint32_t head = smp_load_acquire(circ->head);
	uint32_t tail = circ->tail;
	uint32_t data_cnt = CIRC_CNT(head,tail,circ->length);
	uint32_t data_to_read = data_cnt < len ? data_cnt : len;

	// round data to read down to be dividible by 4
	data_to_read &= 0xFFFFFFFC;

	if (data_to_read >= 1) 
	{
		// copy data from the buffer
		copy_to_user(buff,&circ->buffer[head],data_to_read*sizeof(uint32_t));

		/* Finish reading descriptor before incrementing tail. */
		smp_store_release(circ->tail,
				  (tail + 1) & (circ->length - 1));
	}

	return data_to_read;

}

static struct file_operations timer_cdev_fops = 
{
	.open = timer_cdev_open,
	.mmap = timer_cdev_mmap,
	.close = timer_cdev_close,
	.read = timer_cdev_read,
	.unlocked_ioctl = timer_cdev_ioctl
}



static irqreturn_t timer_irq_handler(int irq, void *data)
{
	struct DMTimer_priv *timer= (struct DMTimer_priv*)data;
	// clear interrupt line and save the input capture flag
	uint32_t irq_status;
	uint32_t icap;
	uint32_t head,tail;

	irq_status = readl_relaxed(timer->io_base + TIMER_IRQSTATUS_OFFSET);
	// clear interrupt flag
	iowrite32(timer->io_base + TIMER_IRQSTATUS_OFFSET,irq_status);

	if(irq_status & TCAR_IT_FLAG)
	{
		icap = readl_relaxed(timer->io_base + TIMER_TCAR1_OFFSET);
		// put input capture data to the circular buffer

		head = timer->circ.head;
		tail = READ_ONCE(timer->circ.tail);

		if (CIRC_SPACE(timer->circ_buf.head, timer->circ_buf.tail, timer->circ_buf.length) >= 1) 
		{
			/* insert one item into the buffer */
			timer->circ_buf.buffer[head] = icap;

			smp_store_release(timer->circ_buf.head,
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
	char *timer_name;
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
	if(unlikely(NULL = irq))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get interrupt.\n",__func__,__line__);
		return -ENODEV;
	}
	mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(unlikely(NULL = mem))
	{
		dev_err(&dev->pdev,"%s:%d: Cannot get timer memory.\n",__func__,__line__);
		return -ENODEV;
	}

	// alloc private descriptor struct
	priv = devm_kzalloc(&pdev->dev,sizeof(struct DMTimer_priv),GFP_KERNEL);
	if(!priv)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot allocate memory.\n",__func__,__line__);
		return -ENOMEM;
	}
	priv->pdev = pdev;
	priv->idx = timer_idx;
	priv->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "DMTimer%d",timer_idx);

	// remapping registers
	priv->io_base =  devm_ioremap_resource(&pdev->dev, mem);
 	if (IS_ERR(priv->io_base))
 	{
 		dev_err(&pdev->dev,"%s,%d: Cannot remap the timer memory range.\n",__func__,__line__);
 		return PTR_ERR(base);
 	}
 	priv->regspace_size = resource_size(mem);
 	priv->regspace_phys_base = mem->start;

 	// remapping irq
 	priv->irq = irq->start;
 	irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_irq",
					  dev_name(dev),timer_idx);
 	ret = devm_request_irq(&pdev->dev,priv->irq,timer_irq_handler,0,irq_name,priv);
 	if(unlikely(ret))
 	{
 		dev_err(&pdev->dev, "%s:%d Interrupt allocation (irq:%d) failed [%d].\n", __func__,__line__,irq,ret);
		return -ENXIO;
 	}


 	// get timer clock
 	priv->fclk = devm_clk_get(&pdev->dev,"fck");
 	if(IS_ERR(priv->fclk))
 	{
 		dev_err(&pdev->dev,"%s:%d Cannot get clock for the timer.\n",__fuction__,__line__);
 		return -ENODEV;
 	}

 	// init circular buffer
 	priv->circ_buf.head = priv->circ_buf.tail = priv->circ_buf.ovf = 0;
 	priv->circ_buf.length = 256;
 	priv->circ_buf.buffer = devm_kzalloc(&pdev->dev,priv->circ_buf.length * sizeof(uint32_t), GFP_KERNEL);


 	// setup misc character device
 	priv->misc.fops = &timer_cdev_fops;
	priv->misc.minor = MISC_DYNAMIC_MINOR;
	priv->misc.name =priv->name;
	if(misc_register(&priv->misc))
	{
		dev_err(&pdev->dev,"Couldn't initialize miscdevice /dev/%s.\n",priv->misc.name);
		return -ENODEV;
	}


	dev_info(&pdev->dev,"%s initialization succeeded.\n",priv->name);
	dev_info(&pdev->dev,"Base address: 0x%x, length: %d, irq num: %d.\n",priv->regspace_phys_base,priv->regspace_size,priv->irq);

 	platform_set_drvdata(pdev,priv);

 	return 0;

}

static int timer_remove(struct platform_device *pdev)
{
	struct DMTimer_priv *priv;
	priv = platform_get_drvdata(pdev);

	misc_deregister(&priv->misc);

	// all other resourcees are freed managed
	return 0;
}				





static struct of_device_id fpga_sched_of_match[] = {
	{ .compatible = "custom_dmtimer", },
	{ }
};



static struct platform_driver fpga_sched_platform_driver = {
	.probe = fpga_sched_probe,
	.remove = fpga_sched_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = fpga_sched_of_match,
	},
};





MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBone timers.");