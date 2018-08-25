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
#include "../icap_channel/icap_channel.h"


// DMTimer register offsets
#define ECAP_TSCTR					0x00
#define ECAP_CTRPHS			 		0x04
#define ECAP_CAP1					0x08
#define ECAP_CAP2					0x0C
#define ECAP_CAP3					0x10
#define ECAP_CAP4					0x14
#define ECAP_ECCTL1					0x28
#define ECAP_ECCTL2					0x2A
#define ECAP_ECEINT					0x2C
#define ECAP_ECFLG					0x2E
	#define ECAP_ECFLG_CEVT1			(1<<1)
	#define ECAP_ECFLG_CEVT2			(1<<2)
	#define ECAP_ECFLG_CEVT3			(1<<3)
	#define ECAP_ECFLG_CEVT4			(1<<4)
	#define ECAP_ECFLG_CNTOVF			(1<<5)
#define ECAP_ECCLR					0x30
#define ECAP_ECFRC					0x32


/*
TODO:
	check ecap clock source

*/

// type declarations

struct ecap_priv
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

	char * icap_channel_name;
	struct icap_channel *icap;
};


// <------------------- CLOCK SOURCE SETTINGS ------------------->
#define CLK_DISABLE		0x00
#define CLK_ENABLE		0x01

static int ecap_set_clk_state(struct ecap_priv *ecap, u32 arg)
{
	int ret = 0;

	if(unlikely(arg > 1) || unlikely(!ecap) || IS_ERR(ecap->fclk))
		return -EINVAL;

	switch(arg)
	{
		case CLK_DISABLE:
			clk_disable(ecap->fclk);
			break;
		case CLK_ENABLE:
			ret = clk_enable(ecap->fclk);
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}


// <------------------------  Timer character device operations ------------------------>

static ssize_t ecap_cdev_open(struct inode *inode, struct file *pfile)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static int ecap_cdev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
	return 0;
}



static int ecap_cdev_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (filep->private_data);
	struct ecap_priv *ecap= container_of(misc_dev, struct ecap_priv, misc);

	// the offset is not used
	unsigned long physical = ecap->regspace_phys_base;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long psize = ecap->regspace_size;

	if (vsize > psize)
	    return -EINVAL; /*  spans too high */

	remap_pfn_range(vma, vma->vm_start, physical, vsize, vma->vm_page_prot);
	return 0;
}

#define IOCTL_SET_CLOCK_STATE	0x20
#define IOCTL_SET_CLOCK_SOURCE 	0x21

#define IOCTL_SET_ICAP_SOURCE	0x30

static long ecap_cdev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct ecap_priv *ecap = container_of(misc_dev, struct ecap_priv, misc);
	int ret = 0;

	switch(cmd)
	{
		case IOCTL_SET_CLOCK_STATE:
			ret = ecap_set_clk_state(ecap,arg);
			break;
			
		case IOCTL_SET_ICAP_SOURCE:
			ret = event_mux_set_ecap_event(ecap->idx,arg);
			break;

		default:
			dev_err(&ecap->pdev->dev,"Invalid IOCTL command : %d.\n",cmd);
			ret = -ENOTTY;
			break;
	}
	return ret;
}


static struct file_operations ecap_cdev_fops = 
{
	.open = ecap_cdev_open,
	.release = ecap_cdev_close,
	.mmap = ecap_cdev_mmap,
	.unlocked_ioctl = ecap_cdev_ioctl
};



static irqreturn_t ecap_irq_handler(int irq, void *data)
{
	struct ecap_priv *ecap= (struct ecap_priv*)data;
	// clear interrupt line and save the input capture flag
	u8 irq_status;
	u32 ts;

	irq_status = readb(ecap->io_base + ECAP_ECFLG);

	// the CAP1-CAP4 registers are used as fifo,
	// all data are sent to the same channel
	
	if(irq_status & ECAP_ECFLG_CEVT1)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP1),
					irq_status & ECAP_ECFLG_CNTOVF);

	if(irq_status & ECAP_ECFLG_CEVT2)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP2),
					irq_status & ECAP_ECFLG_CNTOVF);
	

	if(irq_status & ECAP_ECFLG_CEVT3)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP3),
					irq_status & ECAP_ECFLG_CNTOVF);
	

	if(irq_status & ECAP_ECFLG_CEVT4)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP4),
					irq_status & ECAP_ECFLG_CNTOVF);

	if(irq_status & ECAP_ECFLG_CNTOVF)
		icap_signal_ecap_ovf(ecap->icap);
	
	
	// clear interrupt flag
	writeb(irq_status,ecap->io_base + ECAP_ECCLR);

	return IRQ_HANDLED;
}


static int ecap_probe(struct platform_device *pdev)
{
	const char *ecap_name;
	int ecap_idx;
	struct resource *mem, *irq;
	struct ecap_priv *ecap;
	int ret;
	char *irq_name;

	// read up ecap index
	of_property_read_string_index(pdev->dev.of_node, "interrupt-names", 0, &ecap_name);
	sscanf(ecap_name, "ecap%d", &ecap_idx);
	if(ecap_idx < 0 || ecap_idx > 2) 
	{
		pr_err("Invalid ecap index found in the device tree: %d\n",ecap_idx);
		return -EINVAL;
	}


	// ecap is valid, so lets get its resources
	irq = platform_get_resource(pdev,IORESOURCE_IRQ,0);
	if(unlikely( NULL  ==  irq))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get interrupt.\n",__func__,__LINE__);
		return -ENODEV;
	}
	mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(unlikely(NULL == mem))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get ecap memory.\n",__func__,__LINE__);
		return -ENODEV;
	}

	// alloc private descriptor struct
	ecap = devm_kzalloc(&pdev->dev,sizeof(struct ecap_priv),GFP_KERNEL);
	if(!ecap)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot allocate memory.\n",__func__,__LINE__);
		return -ENOMEM;
	}
	ecap->pdev = pdev;
	ecap->idx = ecap_idx;
	ecap->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "ecap%d",ecap_idx);

	// remapping registers
	ecap->io_base =  devm_ioremap_resource(&pdev->dev, mem);
 	if (IS_ERR(ecap->io_base))
 	{
 		dev_err(&pdev->dev,"%s,%d: Cannot remap the ecap memory range.\n",__func__,__LINE__);
 		return PTR_ERR(ecap->io_base);
 	}
 	ecap->regspace_size = resource_size(mem);
 	ecap->regspace_phys_base = mem->start;

 	// get ecap clock
 	ecap->fclk = devm_clk_get(&pdev->dev,"fck");
 	if(IS_ERR(ecap->fclk))
 	{
 		dev_err(&pdev->dev,"%s:%d Cannot get clock for the ecap.\n",__FUNCTION__,__LINE__);
 		return -ENODEV;
 	}

 	// setup misc character device
 	ecap->misc.fops = &ecap_cdev_fops;
	ecap->misc.minor = MISC_DYNAMIC_MINOR;
	ecap->misc.name =ecap->name;
	if(misc_register(&ecap->misc))
	{
		dev_err(&pdev->dev,"Couldn't initialize miscdevice /dev/%s.\n",ecap->misc.name);
		return -ENODEV;
	}

	ecap->icap_channel_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_icap",dev_name(&pdev->dev),ecap_idx);
	ecap->icap = icap_create_channel(ecap->icap_channel_name,10);
	if(!ecap->icap)
	{
		dev_err(&pdev->dev,"Cannot initialize icap channel: /dev/%s.\n",ecap->misc.name);
		misc_deregister(&ecap->misc);
		return -ENOMEM;
	}

	 // remapping irq
 	ecap->irq = irq->start;
 	irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_irq",
					  dev_name(&pdev->dev),ecap_idx);
 	ret = devm_request_irq(&pdev->dev,ecap->irq,ecap_irq_handler,0,irq_name,ecap);
 	if(unlikely(ret))
 	{
 		dev_err(&pdev->dev, "%s:%d Interrupt allocation (irq:%d) failed [%d].\n", __func__,__LINE__,ecap->irq,ret);
 		icap_delete_channel(ecap->icap);
 		misc_deregister(&ecap->misc);
		return -ENXIO;
 	}


	dev_info(&pdev->dev,"%s initialization succeeded.\n",ecap->name);
	dev_info(&pdev->dev,"Base address: 0x%lx, length: %u, irq num: %d.\n",ecap->regspace_phys_base,ecap->regspace_size,ecap->irq);

 	platform_set_drvdata(pdev,ecap);


 	// start the ecap interface clock
 	pm_runtime_enable(&pdev->dev);
 	if(pm_runtime_get_sync(&pdev->dev) < 0)
 	{
 		dev_err(&pdev->dev,"[%s:%d] Cannot resume the ecap.\n",__FUNCTION__,__LINE__);
 	}

 	return 0;

}

static int ecap_remove(struct platform_device *pdev)
{
	struct ecap_priv *ecap;
	priv = platform_get_drvdata(pdev);

	// stop the ecap interface clock
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	devm_free_irq(&pdev->dev, ecap->irq, ecap);
	
	icap_delete_channel(ecap->icap);
	misc_deregister(&ecap->misc);

	// all other resourcees are freed by the manager layer
	return 0;
}				





static struct of_device_id ecap_of_match[] = {
	{ .compatible	= "ti,am3352-ecap" },
	{ .compatible	= "ti,am33xx-ecap" },
	{}
};

MODULE_DEVICE_TABLE(of, ecap_of_match);

static struct platform_driver ecap_platform_driver = {
	.probe = ecap_probe,
	.remove = ecap_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "RTIO_ECAP",
		.of_match_table = ecap_of_match,
	},
};


module_platform_driver(ecap_platform_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBone ecaps.");